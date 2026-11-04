/* Original scoped file-access minifilter. Kernel implementation stays private. */
#include <fltKernel.h>
#include "FileFilter.h"
#include "Rules.h"
#define FF_TAG 'lFfT'
C_ASSERT(sizeof(FF_RULE)==672);
C_ASSERT(sizeof(FF_EVENT)==1352);
C_ASSERT(sizeof(FF_STATUS)==96);
typedef struct FF_CAPTURE { FF_EVENT Event; } FF_CAPTURE;
typedef struct FF_STATE {
    PFLT_FILTER Filter;
    PFLT_PORT ServerPort,ClientPort;
    EX_RUNDOWN_REF Lifecycle,Session;
    FAST_MUTEX Control;
    EX_PUSH_LOCK PolicyLock;
    KSPIN_LOCK RingLock;
    FF_RULE Rules[FF_RULE_LIMIT];
    PEPROCESS Processes[FF_RULE_LIMIT];
    volatile LONG RuleCount,Connected,Messages,Inflight;
    FF_U64 Generation,Frequency,NextSequence,Observed,Denied,Dropped;
    volatile LONG64 NameFailures,Skipped;
    ULONG Head,Count;
    FF_EVENT Ring[FF_RING_CAPACITY];
} FF_STATE;
static FF_STATE g;
static VOID StopPolicy(VOID){
    PEPROCESS previous[FF_RULE_LIMIT];ULONG i;KIRQL irql;
    KeEnterCriticalRegion();ExAcquirePushLockExclusive(&g.PolicyLock);
    for(i=0;i<FF_RULE_LIMIT;++i){previous[i]=g.Processes[i];g.Processes[i]=NULL;}
    InterlockedExchange(&g.RuleCount,0);
    KeAcquireSpinLock(&g.RingLock,&irql);++g.Generation;g.Head=g.Count=0;KeReleaseSpinLock(&g.RingLock,irql);
    ExReleasePushLockExclusive(&g.PolicyLock);KeLeaveCriticalRegion();
    for(i=0;i<FF_RULE_LIMIT;++i)if(previous[i])ObDereferenceObject(previous[i]);
}
static FF_U16 Upper(FF_U16 value){return (FF_U16)RtlUpcaseUnicodeChar((WCHAR)value);}
static VOID CopyName(FF_U16* output,FF_U16* length,PFLT_FILE_NAME_INFORMATION name,FF_U32* flags,FF_U32 truncated){
    ULONG chars;
    if(!name)return;
    chars=name->Name.Length/sizeof(WCHAR);if(chars>=FF_PATH_LIMIT){chars=FF_PATH_LIMIT-1;*flags|=truncated;}
    RtlCopyMemory(output,name->Name.Buffer,chars*sizeof(WCHAR));*length=(FF_U16)chars;
}
static VOID Emit(FF_CAPTURE* context){
    KIRQL irql;FF_EVENT* e=&context->Event;ULONG slot;
    KeAcquireSpinLock(&g.RingLock,&irql);
    if(ReadAcquire(&g.RuleCount)&&e->Generation==g.Generation){
        e->Sequence=g.NextSequence++;++g.Observed;if(e->Flags&FF_FLAG_DENIED)++g.Denied;
        if(g.Count==FF_RING_CAPACITY){g.Head=(g.Head+1)%FF_RING_CAPACITY;--g.Count;++g.Dropped;}
        slot=(g.Head+g.Count)%FF_RING_CAPACITY;g.Ring[slot]=*e;++g.Count;
    }
    KeReleaseSpinLock(&g.RingLock,irql);
}
static VOID Lost(FF_U64 generation,BOOLEAN denied){
    KIRQL irql;KeAcquireSpinLock(&g.RingLock,&irql);
    if(g.Generation==generation){++g.Observed;++g.Dropped;if(denied)++g.Denied;}
    KeReleaseSpinLock(&g.RingLock,irql);
}
static ULONG AccessMask(PFLT_CALLBACK_DATA data){
    ULONG major=data->Iopb->MajorFunction;
    if(major==IRP_MJ_READ)return FF_READ;
    if(major==IRP_MJ_WRITE)return FF_WRITE;
    if(major==IRP_MJ_SET_SECURITY)return FF_SECURITY;
    if(major==IRP_MJ_CREATE){
        ACCESS_MASK access=data->Iopb->Parameters.Create.SecurityContext->DesiredAccess;
        ULONG result=FF_OPEN,options=data->Iopb->Parameters.Create.Options,disposition=options>>24;
        if(access&MAXIMUM_ALLOWED)return FF_ALL;
        if(access&(FILE_READ_DATA|FILE_READ_EA|FILE_READ_ATTRIBUTES|GENERIC_READ|GENERIC_ALL))result|=FF_READ;
        if((access&(FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|GENERIC_WRITE|GENERIC_ALL))||disposition==FILE_CREATE||disposition==FILE_OPEN_IF||disposition==FILE_OVERWRITE||disposition==FILE_OVERWRITE_IF||disposition==FILE_SUPERSEDE)result|=FF_WRITE;
        if((access&(DELETE|FILE_DELETE_CHILD|GENERIC_ALL))||(options&FILE_DELETE_ON_CLOSE)||disposition==FILE_SUPERSEDE)result|=FF_DELETE;
        if(access&(WRITE_DAC|WRITE_OWNER|GENERIC_ALL))result|=FF_SECURITY;
        if(access&(FILE_EXECUTE|GENERIC_EXECUTE|GENERIC_ALL))result|=FF_EXECUTE_OPEN;
        return result;
    }
    if(major==IRP_MJ_SET_INFORMATION){
        FILE_INFORMATION_CLASS kind=data->Iopb->Parameters.SetFileInformation.FileInformationClass;
        if(kind==FileRenameInformation||kind==FileRenameInformationEx)return FF_RENAME_LINK|FF_DELETE;
        if(kind==FileLinkInformation||kind==FileLinkInformationEx){
            ULONG result=FF_RENAME_LINK;
            __try{
                if(data->Iopb->Parameters.SetFileInformation.Length>=sizeof(ULONG)&&
                   (*(PULONG)data->Iopb->Parameters.SetFileInformation.InfoBuffer&1u))result|=FF_DELETE;
            }__except(FsRtlIsNtstatusExpected(GetExceptionCode())?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return 0;}
            return result;
        }
        if(kind==FileDispositionInformation||kind==FileDispositionInformationEx){
            __try{
                if(kind==FileDispositionInformation&&data->Iopb->Parameters.SetFileInformation.Length>=sizeof(FILE_DISPOSITION_INFORMATION))
                    return ((PFILE_DISPOSITION_INFORMATION)data->Iopb->Parameters.SetFileInformation.InfoBuffer)->DeleteFile?FF_DELETE:0;
                if(kind==FileDispositionInformationEx&&data->Iopb->Parameters.SetFileInformation.Length>=sizeof(FILE_DISPOSITION_INFORMATION_EX))
                    return (((PFILE_DISPOSITION_INFORMATION_EX)data->Iopb->Parameters.SetFileInformation.InfoBuffer)->Flags&FILE_DISPOSITION_DELETE)?FF_DELETE:0;
            }__except(FsRtlIsNtstatusExpected(GetExceptionCode())?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return 0;}
            return 0;
        }
        return FF_METADATA|(kind==FileEndOfFileInformation||kind==FileAllocationInformation||kind==FileValidDataLengthInformation?FF_WRITE:0);
    }
    return 0;
}
static PFLT_FILE_NAME_INFORMATION Destination(PFLT_CALLBACK_DATA data,PCFLT_RELATED_OBJECTS objects){
    PFILE_RENAME_INFORMATION rename=data->Iopb->Parameters.SetFileInformation.InfoBuffer;
    PFLT_FILE_NAME_INFORMATION name=NULL;HANDLE root=NULL;ULONG length=0,total=data->Iopb->Parameters.SetFileInformation.Length;
    PWCHAR copy=NULL;NTSTATUS status=STATUS_INVALID_PARAMETER;
    if(total<(ULONG)FIELD_OFFSET(FILE_RENAME_INFORMATION,FileName))return NULL;
    __try{
        length=rename->FileNameLength;root=rename->RootDirectory;
        if(length&&!(length&1)&&length<=32768&&length<=total-FIELD_OFFSET(FILE_RENAME_INFORMATION,FileName)){
            copy=ExAllocatePool2(POOL_FLAG_PAGED,length,FF_TAG);
            if(copy){RtlCopyMemory(copy,rename->FileName,length);status=STATUS_SUCCESS;}
        }
    }__except(FsRtlIsNtstatusExpected(GetExceptionCode())?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){status=GetExceptionCode();}
    if(NT_SUCCESS(status))status=FltGetDestinationFileNameInformation(objects->Instance,objects->FileObject,root,copy,length,FLT_FILE_NAME_NORMALIZED|FLT_FILE_NAME_QUERY_DEFAULT,&name);
    if(copy)ExFreePoolWithTag(copy,FF_TAG);
    return NT_SUCCESS(status)?name:NULL;
}
static FLT_PREOP_CALLBACK_STATUS Pre(PFLT_CALLBACK_DATA data,PCFLT_RELATED_OBJECTS objects,PVOID* completion){
    PFLT_FILE_NAME_INFORMATION name=NULL,target=NULL;NTSTATUS status;ULONG operations,i,id=0,pid,action=FF_AUDIT;
    FF_U64 generation=0;FF_CAPTURE* context=NULL;BOOLEAN candidate=FALSE;PEPROCESS process;
    *completion=NULL;
    if(!ReadAcquire(&g.RuleCount))return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if(data->RequestorMode!=UserMode||KeGetCurrentIrql()!=PASSIVE_LEVEL||IoGetTopLevelIrp()!=NULL||
       (FLT_IS_IRP_OPERATION(data)&&(data->Iopb->IrpFlags&IRP_PAGING_IO))){InterlockedIncrement64(&g.Skipped);return FLT_PREOP_SUCCESS_NO_CALLBACK;}
    process=FltGetRequestorProcess(data);pid=FltGetRequestorProcessId(data);operations=AccessMask(data);if(!operations)return FLT_PREOP_SUCCESS_NO_CALLBACK;
    KeEnterCriticalRegion();ExAcquirePushLockShared(&g.PolicyLock);
    for(i=0;i<(ULONG)g.RuleCount;++i)if((!g.Processes[i]||g.Processes[i]==process)&&(g.Rules[i].Operations&operations)){candidate=TRUE;break;}
    ExReleasePushLockShared(&g.PolicyLock);KeLeaveCriticalRegion();if(!candidate)return FLT_PREOP_SUCCESS_NO_CALLBACK;
    status=FltGetFileNameInformation(data,FLT_FILE_NAME_NORMALIZED|FLT_FILE_NAME_QUERY_DEFAULT,&name);
    if(!NT_SUCCESS(status)){InterlockedIncrement64(&g.NameFailures);return FLT_PREOP_SUCCESS_NO_CALLBACK;}
    if(operations&FF_RENAME_LINK){target=Destination(data,objects);if(!target)InterlockedIncrement64(&g.NameFailures);}
    KeEnterCriticalRegion();ExAcquirePushLockShared(&g.PolicyLock);
    i=FfSelect(g.Rules,(const void* const*)g.Processes,(ULONG)g.RuleCount,process,operations,
        (FF_U16*)name->Name.Buffer,name->Name.Length/2,target?(FF_U16*)target->Name.Buffer:NULL,target?target->Name.Length/2:0,Upper);
    if(i<FF_RULE_LIMIT){id=g.Rules[i].Id;action=g.Rules[i].Action;generation=g.Generation;}
    ExReleasePushLockShared(&g.PolicyLock);KeLeaveCriticalRegion();
    if(!id)goto done;
    if(action==FF_DENY&&FLT_IS_FASTIO_OPERATION(data)){
        FltReleaseFileNameInformation(name);if(target)FltReleaseFileNameInformation(target);
        return FLT_PREOP_DISALLOW_FASTIO;
    }
    if(InterlockedIncrement(&g.Inflight)<=FF_CONTEXT_LIMIT)context=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(FF_CAPTURE),FF_TAG);
    if(!context){InterlockedDecrement(&g.Inflight);Lost(generation,action==FF_DENY);}
    else{
        RtlZeroMemory(context,sizeof(*context));context->Event.Generation=generation;context->Event.ProcessId=pid;
        context->Event.RuleId=id;context->Event.Operations=operations;context->Event.StartQpc=(FF_U64)KeQueryPerformanceCounter(NULL).QuadPart;
        if(data->Iopb->MajorFunction==IRP_MJ_READ)context->Event.RequestedBytes=data->Iopb->Parameters.Read.Length;
        if(data->Iopb->MajorFunction==IRP_MJ_WRITE)context->Event.RequestedBytes=data->Iopb->Parameters.Write.Length;
        if(data->Iopb->MajorFunction==IRP_MJ_CREATE||operations&FF_RENAME_LINK)context->Event.Flags|=FF_FLAG_PATH_INTENT;
        if(operations&FF_RENAME_LINK&&!target)context->Event.Flags|=FF_FLAG_TARGET_UNKNOWN;
        CopyName(context->Event.Path,&context->Event.PathLength,name,&context->Event.Flags,FF_FLAG_PATH_TRUNCATED);
        CopyName(context->Event.Target,&context->Event.TargetLength,target,&context->Event.Flags,FF_FLAG_TARGET_TRUNCATED);
    }
done:
    FltReleaseFileNameInformation(name);if(target)FltReleaseFileNameInformation(target);
    if(id&&action==FF_DENY){
        data->IoStatus.Status=STATUS_ACCESS_DENIED;data->IoStatus.Information=0;
        if(context){context->Event.Status=(FF_U32)STATUS_ACCESS_DENIED;context->Event.Flags|=FF_FLAG_DENIED;Emit(context);ExFreePoolWithTag(context,FF_TAG);InterlockedDecrement(&g.Inflight);}
        return FLT_PREOP_COMPLETE;
    }
    if(context){*completion=context;return FLT_PREOP_SUCCESS_WITH_CALLBACK;}
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
static FLT_POSTOP_CALLBACK_STATUS Post(PFLT_CALLBACK_DATA data,PCFLT_RELATED_OBJECTS objects,PVOID completion,FLT_POST_OPERATION_FLAGS flags){
    FF_CAPTURE* context=completion;FF_U64 end;UNREFERENCED_PARAMETER(objects);
    if(!(flags&FLTFL_POST_OPERATION_DRAINING)){
        end=(FF_U64)KeQueryPerformanceCounter(NULL).QuadPart;context->Event.DurationQpc=end>=context->Event.StartQpc?end-context->Event.StartQpc:0;
        context->Event.Status=(FF_U32)data->IoStatus.Status;
        if(NT_SUCCESS(data->IoStatus.Status)&&context->Event.RequestedBytes)context->Event.TransferredBytes=min((FF_U64)data->IoStatus.Information,context->Event.RequestedBytes);
        Emit(context);
    }
    ExFreePoolWithTag(context,FF_TAG);InterlockedDecrement(&g.Inflight);return FLT_POSTOP_FINISHED_PROCESSING;
}
static NTSTATUS Setup(PCFLT_RELATED_OBJECTS objects,FLT_INSTANCE_SETUP_FLAGS flags,DEVICE_TYPE type,FLT_FILESYSTEM_TYPE fs){
    UNREFERENCED_PARAMETER(objects);UNREFERENCED_PARAMETER(flags);
    return type==FILE_DEVICE_DISK_FILE_SYSTEM&&(fs==FLT_FSTYPE_NTFS||fs==FLT_FSTYPE_REFS)?STATUS_SUCCESS:STATUS_FLT_DO_NOT_ATTACH;
}
static NTSTATUS Teardown(PCFLT_RELATED_OBJECTS objects,FLT_INSTANCE_QUERY_TEARDOWN_FLAGS flags){UNREFERENCED_PARAMETER(objects);UNREFERENCED_PARAMETER(flags);return STATUS_SUCCESS;}
static NTSTATUS Connect(PFLT_PORT port,PVOID serverCookie,PVOID connectionContext,ULONG size,PVOID* cookie){
    UNREFERENCED_PARAMETER(serverCookie);UNREFERENCED_PARAMETER(connectionContext);if(size)return STATUS_ACCESS_DENIED;
    KeEnterCriticalRegion();if(!ExAcquireRundownProtection(&g.Lifecycle)){KeLeaveCriticalRegion();return STATUS_FLT_DELETING_OBJECT;}
    if(InterlockedCompareExchange(&g.Connected,1,0)){ExReleaseRundownProtection(&g.Lifecycle);KeLeaveCriticalRegion();return STATUS_ACCESS_DENIED;}
    ExInitializeRundownProtection(&g.Session);StopPolicy();g.ClientPort=port;*cookie=&g;
    ExReleaseRundownProtection(&g.Lifecycle);KeLeaveCriticalRegion();return STATUS_SUCCESS;
}
static VOID Disconnect(PVOID cookie){
    UNREFERENCED_PARAMETER(cookie);ExWaitForRundownProtectionRelease(&g.Session);
    StopPolicy();FltCloseClientPort(g.Filter,&g.ClientPort);InterlockedExchange(&g.Connected,0);
}
static NTSTATUS Replace(FF_POLICY* policy){
    PEPROCESS next[FF_RULE_LIMIT]={0},old[FF_RULE_LIMIT];ULONG i,j;NTSTATUS status=STATUS_SUCCESS;KIRQL irql;
    for(i=0;i<policy->Header.Count;++i){
        if(!FfValidRule(&policy->Rules[i])){status=STATUS_INVALID_PARAMETER;goto fail;}
        for(j=0;j<i;++j)if(policy->Rules[i].Id==policy->Rules[j].Id){status=STATUS_INVALID_PARAMETER;goto fail;}
        if(policy->Rules[i].ProcessId){status=PsLookupProcessByProcessId(ULongToHandle(policy->Rules[i].ProcessId),&next[i]);if(!NT_SUCCESS(status))goto fail;}
    }
    KeEnterCriticalRegion();ExAcquirePushLockExclusive(&g.PolicyLock);
    for(i=0;i<FF_RULE_LIMIT;++i){old[i]=g.Processes[i];g.Processes[i]=next[i];next[i]=NULL;}
    RtlCopyMemory(g.Rules,policy->Rules,policy->Header.Count*sizeof(FF_RULE));
    KeAcquireSpinLock(&g.RingLock,&irql);++g.Generation;g.Head=g.Count=0;g.Dropped=g.Observed=g.Denied=0;g.NameFailures=g.Skipped=0;g.NextSequence=1;
    InterlockedExchange(&g.RuleCount,(LONG)policy->Header.Count);KeReleaseSpinLock(&g.RingLock,irql);
    ExReleasePushLockExclusive(&g.PolicyLock);KeLeaveCriticalRegion();
    for(i=0;i<FF_RULE_LIMIT;++i)if(old[i])ObDereferenceObject(old[i]);return STATUS_SUCCESS;
fail:for(i=0;i<FF_RULE_LIMIT;++i)if(next[i])ObDereferenceObject(next[i]);return status;
}
static NTSTATUS Inner(PVOID input,ULONG inputLength,PVOID output,ULONG outputLength,PULONG returned){
    FF_COMMAND header;FF_POLICY* policy=NULL;FF_REPLY* reply=NULL;NTSTATUS status=STATUS_SUCCESS;KIRQL irql;ULONG i,count=0,bytes;
    *returned=0;if(!input||inputLength<sizeof(header)||!output||outputLength<sizeof(FF_STATUS))return STATUS_INVALID_PARAMETER;
    __try{RtlCopyMemory(&header,input,sizeof(header));}__except(FsRtlIsNtstatusExpected(GetExceptionCode())?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return GetExceptionCode();}
    if(header.Magic!=FF_MAGIC||header.Version!=FF_VERSION||header.Size!=inputLength||header.Reserved||header.Operation<1||header.Operation>4||
       (header.Operation==FF_COMMAND_POLICY?(inputLength!=sizeof(FF_POLICY)||header.Count>FF_RULE_LIMIT):(inputLength!=sizeof(header)||header.Count)))return STATUS_INVALID_PARAMETER;
    reply=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*reply),FF_TAG);if(!reply)return STATUS_INSUFFICIENT_RESOURCES;RtlZeroMemory(reply,sizeof(*reply));
    if(header.Operation==FF_COMMAND_POLICY){
        policy=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*policy),FF_TAG);if(!policy){status=STATUS_INSUFFICIENT_RESOURCES;goto done;}
        __try{RtlCopyMemory(policy,input,sizeof(*policy));}__except(FsRtlIsNtstatusExpected(GetExceptionCode())?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){status=GetExceptionCode();goto done;}
        /* Treat the callback input as untrusted mutable user memory. */
        if(RtlCompareMemory(&policy->Header,&header,sizeof(header))!=sizeof(header)){status=STATUS_INVALID_PARAMETER;goto done;}
        status=Replace(policy);if(!NT_SUCCESS(status))goto done;
    }else if(header.Operation==FF_COMMAND_STOP)StopPolicy();
    KeAcquireSpinLock(&g.RingLock,&irql);
    if(header.Operation==FF_COMMAND_READ){count=min(g.Count,min(FF_READ_LIMIT,(outputLength-(ULONG)sizeof(FF_STATUS))/(ULONG)sizeof(FF_EVENT)));for(i=0;i<count;++i)reply->Events[i]=g.Ring[(g.Head+i)%FF_RING_CAPACITY];g.Head=(g.Head+count)%FF_RING_CAPACITY;g.Count-=count;}
    reply->Status.Magic=FF_MAGIC;reply->Status.Version=FF_VERSION;reply->Status.Size=sizeof(FF_STATUS)+count*sizeof(FF_EVENT);reply->Status.Count=count;
    reply->Status.RuleCount=(ULONG)g.RuleCount;reply->Status.Inflight=min((ULONG)g.Inflight,FF_CONTEXT_LIMIT);reply->Status.Capacity=FF_RING_CAPACITY;
    reply->Status.Generation=g.Generation;reply->Status.Frequency=g.Frequency;reply->Status.Observed=g.Observed;reply->Status.Denied=g.Denied;reply->Status.Dropped=g.Dropped;
    reply->Status.NameFailures=(FF_U64)g.NameFailures;reply->Status.Skipped=(FF_U64)g.Skipped;reply->Status.NextSequence=g.NextSequence;
    KeReleaseSpinLock(&g.RingLock,irql);bytes=reply->Status.Size;
    __try{RtlCopyMemory(output,reply,bytes);*returned=bytes;}
    __except(FsRtlIsNtstatusExpected(GetExceptionCode())?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){status=GetExceptionCode();KeAcquireSpinLock(&g.RingLock,&irql);if(g.Generation==reply->Status.Generation)g.Dropped+=count;KeReleaseSpinLock(&g.RingLock,irql);}
done:if(policy)ExFreePoolWithTag(policy,FF_TAG);ExFreePoolWithTag(reply,FF_TAG);return status;
}
static NTSTATUS Message(PVOID cookie,PVOID input,ULONG inputLength,PVOID output,ULONG outputLength,PULONG returned){
    NTSTATUS status;UNREFERENCED_PARAMETER(cookie);*returned=0;KeEnterCriticalRegion();
    if(!ExAcquireRundownProtection(&g.Lifecycle)){KeLeaveCriticalRegion();return STATUS_FLT_DELETING_OBJECT;}
    if(!ExAcquireRundownProtection(&g.Session)){ExReleaseRundownProtection(&g.Lifecycle);KeLeaveCriticalRegion();return STATUS_PORT_DISCONNECTED;}
    if(InterlockedIncrement(&g.Messages)>4){status=STATUS_DEVICE_BUSY;goto done;}
    ExAcquireFastMutex(&g.Control);status=Inner(input,inputLength,output,outputLength,returned);ExReleaseFastMutex(&g.Control);
done:InterlockedDecrement(&g.Messages);ExReleaseRundownProtection(&g.Session);ExReleaseRundownProtection(&g.Lifecycle);KeLeaveCriticalRegion();return status;
}
static NTSTATUS Unload(FLT_FILTER_UNLOAD_FLAGS flags){
    UNREFERENCED_PARAMETER(flags);FltCloseCommunicationPort(g.ServerPort);ExWaitForRundownProtectionRelease(&g.Lifecycle);StopPolicy();FltUnregisterFilter(g.Filter);
    NT_ASSERT(g.Inflight==0);return STATUS_SUCCESS;
}
static const FLT_OPERATION_REGISTRATION Operations[]={
    {IRP_MJ_CREATE,0,Pre,Post},{IRP_MJ_READ,FLTFL_OPERATION_REGISTRATION_SKIP_PAGING_IO,Pre,Post},
    {IRP_MJ_WRITE,FLTFL_OPERATION_REGISTRATION_SKIP_PAGING_IO,Pre,Post},{IRP_MJ_SET_INFORMATION,0,Pre,Post},
    {IRP_MJ_SET_SECURITY,0,Pre,Post},{IRP_MJ_OPERATION_END}
};
static const FLT_REGISTRATION Registration={sizeof(FLT_REGISTRATION),FLT_REGISTRATION_VERSION,0,NULL,Operations,Unload,Setup,Teardown,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
DRIVER_INITIALIZE DriverEntry;
NTSTATUS DriverEntry(PDRIVER_OBJECT object,PUNICODE_STRING registry){
    NTSTATUS status;PSECURITY_DESCRIPTOR security=NULL;OBJECT_ATTRIBUTES attributes;UNICODE_STRING name;LARGE_INTEGER frequency;
    UNREFERENCED_PARAMETER(registry);RtlZeroMemory(&g,sizeof(g));KeInitializeSpinLock(&g.RingLock);ExInitializePushLock(&g.PolicyLock);ExInitializeFastMutex(&g.Control);ExInitializeRundownProtection(&g.Lifecycle);
    KeQueryPerformanceCounter(&frequency);g.Frequency=(FF_U64)frequency.QuadPart;g.Generation=g.NextSequence=1;
    status=FltRegisterFilter(object,&Registration,&g.Filter);if(!NT_SUCCESS(status))goto fail;
    status=FltBuildDefaultSecurityDescriptor(&security,FLT_PORT_ALL_ACCESS);if(!NT_SUCCESS(status))goto unregister;
    RtlInitUnicodeString(&name,FF_PORT_NAME);InitializeObjectAttributes(&attributes,&name,OBJ_CASE_INSENSITIVE|OBJ_KERNEL_HANDLE,NULL,security);
    status=FltCreateCommunicationPort(g.Filter,&g.ServerPort,&attributes,NULL,Connect,Disconnect,Message,1);FltFreeSecurityDescriptor(security);if(!NT_SUCCESS(status))goto unregister;
    status=FltStartFiltering(g.Filter);if(NT_SUCCESS(status))return status;FltCloseCommunicationPort(g.ServerPort);
unregister:ExWaitForRundownProtectionRelease(&g.Lifecycle);StopPolicy();FltUnregisterFilter(g.Filter);
fail:return status;
}
