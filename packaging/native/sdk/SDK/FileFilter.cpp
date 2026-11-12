#include "FileFilter.h"
#include <windows.h>
#include <fltuser.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <sstream>
namespace FileFilter {
namespace {
[[noreturn]] void Fail(const char* stage,HRESULT code){
    const char* detail="Filter Manager or Windows operation failed";
    if(code==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)||code==HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))detail="Driver is not running; its allocated-altitude and Microsoft-signed package is required";
    if(code==E_ACCESSDENIED)detail="Administrator access is required, or another application owns the filter port";
    if(code==HRESULT_FROM_WIN32(ERROR_INVALID_DATA))detail="Driver response is malformed or incompatible with this SDK";
    std::ostringstream error;error<<stage<<": "<<detail<<" (HRESULT 0x"<<std::hex<<static_cast<unsigned long>(code)<<")";
    throw std::runtime_error(error.str());
}
void Check(HRESULT result,const char* stage){if(FAILED(result))Fail(stage,result);}
std::wstring NativeFolder(const std::wstring& path){
    HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(file==INVALID_HANDLE_VALUE)Fail("Open existing folder",HRESULT_FROM_WIN32(GetLastError()));
    struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{file};
    FILE_STANDARD_INFO info{};if(!GetFileInformationByHandleEx(file,FileStandardInfo,&info,sizeof(info)))Fail("Read folder attributes",HRESULT_FROM_WIN32(GetLastError()));
    if(!info.Directory)throw std::invalid_argument("Select an existing directory, not an individual file.");
    wchar_t fs[32]{};if(!GetVolumeInformationByHandleW(file,nullptr,0,nullptr,nullptr,nullptr,fs,32))Fail("Read filesystem type",HRESULT_FROM_WIN32(GetLastError()));
    if(_wcsicmp(fs,L"NTFS")&&_wcsicmp(fs,L"ReFS"))throw std::invalid_argument("This preview targets local NTFS/ReFS directories.");
    DWORD length=GetFinalPathNameByHandleW(file,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_NT);
    if(!length||length>=FF_PATH_LIMIT)throw std::invalid_argument("The normalized folder path is unavailable or too long (319 UTF-16 characters maximum).");
    std::wstring native(length,L'\0');DWORD written=GetFinalPathNameByHandleW(file,native.data(),length,FILE_NAME_NORMALIZED|VOLUME_NAME_NT);
    if(!written||written>=length)Fail("Normalize folder name",HRESULT_FROM_WIN32(GetLastError()?GetLastError():ERROR_INVALID_DATA));native.resize(written);
    while(!native.empty()&&native.back()==L'\\')native.pop_back();
    if(native.rfind(L"\\Device\\HarddiskVolume",0)!=0)throw std::invalid_argument("Select a local disk volume directory; UNC and other namespaces are unsupported.");
    return native;
}
}
bool Validate(const FF_RULE& r) noexcept{
    unsigned parts=0;
    if(!r.Id||!r.Operations||(r.Operations&~FF_ALL)||r.Action>FF_DENY||r.ProcessId==4||r.PathLength<19||r.PathLength>=FF_PATH_LIMIT)return false;
    if(r.Reserved[0]||r.Reserved[1]||r.Reserved[2]||r.Folder[r.PathLength])return false;
    const FF_U16 prefix[]={'\\','D','e','v','i','c','e','\\'};
    for(unsigned i=0;i<8;++i)if(r.Folder[i]!=prefix[i])return false;
    for(unsigned i=0;i<r.PathLength;++i){
        auto c=r.Folder[i];if(!c||c<' '||c=='/'||c=='*'||c=='?'||c==':')return false;
        if(c=='\\'){
            ++parts;if(i+1==r.PathLength||r.Folder[i+1]=='\\')return false;
            if(r.Folder[i+1]=='.'&&(i+2==r.PathLength||r.Folder[i+2]=='\\'||(r.Folder[i+2]=='.'&&(i+3==r.PathLength||r.Folder[i+3]=='\\'))))return false;
        }
    }
    return parts>=3;
}
bool Validate(const FF_POLICY& p) noexcept{
    if(p.Header.Magic!=FF_MAGIC||p.Header.Version!=FF_VERSION||p.Header.Size!=sizeof(p)||p.Header.Operation!=FF_COMMAND_POLICY||p.Header.Reserved||p.Header.Count>FF_RULE_LIMIT)return false;
    for(unsigned i=0;i<p.Header.Count;++i){if(!Validate(p.Rules[i]))return false;for(unsigned j=0;j<i;++j)if(p.Rules[j].Id==p.Rules[i].Id)return false;}
    return true;
}
bool Validate(const FF_REPLY& p,std::size_t bytes) noexcept{
    const auto& s=p.Status;
    if(bytes<sizeof(FF_STATUS)||bytes>sizeof(p)||s.Magic!=FF_MAGIC||s.Version!=FF_VERSION||s.Size!=bytes||s.Count>FF_READ_LIMIT||bytes!=sizeof(FF_STATUS)+s.Count*sizeof(FF_EVENT)||s.RuleCount>FF_RULE_LIMIT||(s.Count&&!s.RuleCount)||s.Inflight>FF_CONTEXT_LIMIT||s.Capacity!=FF_RING_CAPACITY||s.Reserved||!s.Frequency||!s.Generation||!s.NextSequence||s.Denied>s.Observed)return false;
    FF_U64 previous=0;
    for(unsigned i=0;i<s.Count;++i){const auto& e=p.Events[i];
        if(!e.Sequence||e.Sequence<=previous||e.Sequence>=s.NextSequence||!e.StartQpc||e.Generation!=s.Generation||!e.RuleId||!e.Operations||(e.Operations&~FF_ALL)||(e.Flags&~31u)||e.PathLength>=FF_PATH_LIMIT||e.TargetLength>=FF_PATH_LIMIT||!e.PathLength||e.Path[e.PathLength]||e.Target[e.TargetLength]||e.TransferredBytes>e.RequestedBytes||((e.Status&0x80000000u)&&e.TransferredBytes))return false;
        if(e.DurationQpc>~FF_U64(0)-e.StartQpc||(e.Flags&FF_FLAG_DENIED)&&(e.Status!=0xC0000022u||e.TransferredBytes||e.DurationQpc))return false;
        previous=e.Sequence;
    }
    return true;
}
FF_RULE Rule(unsigned id,const std::wstring& path,unsigned operations,unsigned action,unsigned processId){
    if(!id||!operations||(operations&~FF_ALL)||action>FF_DENY||processId==4)throw std::invalid_argument("Invalid rule ID, operation mask, action or process selection.");
    auto native=NativeFolder(path);FF_RULE rule{};rule.Id=id;rule.ProcessId=processId;rule.Operations=operations;rule.Action=action;rule.PathLength=static_cast<FF_U32>(native.size());
    std::copy(native.begin(),native.end(),rule.Folder);if(!Validate(rule))throw std::invalid_argument("Choose a directory below the volume root; wildcards and relative scopes are unsupported.");return rule;
}
std::wstring Folder(const FF_RULE& r){return Validate(r)?std::wstring(r.Folder,r.Folder+r.PathLength):L"Invalid scope";}
std::wstring Path(const FF_EVENT& e,bool destination){const auto length=destination?e.TargetLength:e.PathLength;const auto* text=destination?e.Target:e.Path;return length<FF_PATH_LIMIT?std::wstring(text,text+length):L"Invalid path";}
std::wstring Operations(unsigned mask){
    std::wstring result;
    const std::pair<unsigned,const wchar_t*> names[]={{FF_OPEN,L"Open"},{FF_READ,L"Read"},{FF_WRITE,L"Write/create"},{FF_DELETE,L"Delete"},{FF_RENAME_LINK,L"Rename/link"},{FF_METADATA,L"Metadata"},{FF_SECURITY,L"Security"},{FF_EXECUTE_OPEN,L"Execute-open"}};
    for(const auto& item:names)if(mask&item.first){if(!result.empty())result+=L" + ";result+=item.second;}return result;
}
struct Session::State {
    HANDLE port=INVALID_HANDLE_VALUE;
    FF_STATUS last{};
    std::unique_ptr<FF_REPLY> Request(unsigned operation,const FF_POLICY* policy=nullptr){
        if(port==INVALID_HANDLE_VALUE)throw std::runtime_error("Start the file filter session before requesting data.");
        FF_COMMAND header{FF_MAGIC,FF_VERSION,sizeof(FF_COMMAND),operation,0,0};auto reply=std::make_unique<FF_REPLY>();DWORD bytes=0;
        auto result=FilterSendMessage(port,policy?const_cast<FF_POLICY*>(policy):static_cast<void*>(&header),policy?sizeof(FF_POLICY):sizeof(header),reply.get(),sizeof(*reply),&bytes);
        Check(result,"Send file-filter command");if(!Validate(*reply,bytes))Fail("Validate file-filter reply",HRESULT_FROM_WIN32(ERROR_INVALID_DATA));last=reply->Status;return reply;
    }
};
Session::Session():state_(std::make_unique<State>()){}
Session::~Session(){Stop();}
void Session::Start(const std::vector<FF_RULE>& rules){
    if(state_->port!=INVALID_HANDLE_VALUE)throw std::runtime_error("The file filter session is already open.");
    HANDLE port=INVALID_HANDLE_VALUE;Check(FilterConnectCommunicationPort(FF_PORT_NAME,0,nullptr,0,nullptr,&port),"Connect to file filter");state_->port=port;
    try{SetRules(rules);}catch(...){Stop();throw;}
}
void Session::SetRules(const std::vector<FF_RULE>& rules){
    if(rules.size()>FF_RULE_LIMIT)throw std::invalid_argument("At most 32 file rules are supported.");
    auto policy=std::make_unique<FF_POLICY>();policy->Header={FF_MAGIC,FF_VERSION,sizeof(FF_POLICY),FF_COMMAND_POLICY,static_cast<FF_U32>(rules.size()),0};std::copy(rules.begin(),rules.end(),policy->Rules);
    if(!Validate(*policy))throw std::invalid_argument("Invalid folder scope, operations or duplicate rule IDs.");state_->Request(FF_COMMAND_POLICY,policy.get());
}
std::vector<FF_EVENT> Session::Read(){auto reply=state_->Request(FF_COMMAND_READ);return {reply->Events,reply->Events+reply->Status.Count};}
FF_STATUS Session::Status() const{state_->Request(FF_COMMAND_STATUS);return state_->last;}
void Session::Stop() noexcept{if(state_->port!=INVALID_HANDLE_VALUE){CloseHandle(state_->port);state_->port=INVALID_HANDLE_VALUE;}state_->last={};}
}
