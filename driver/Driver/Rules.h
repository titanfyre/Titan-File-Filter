/* Shared by the private kernel and private offline matcher tests. */
#ifndef TITAN_FILE_FILTER_RULE_CORE_H
#define TITAN_FILE_FILTER_RULE_CORE_H
static __inline int FfValidRule(const FF_RULE* r){
    unsigned i,parts=0;
    if(!r->Id||!r->Operations||(r->Operations&~FF_ALL)||r->Action>FF_DENY||r->ProcessId==4||r->PathLength<19||r->PathLength>=FF_PATH_LIMIT)return 0;
    if(r->Reserved[0]||r->Reserved[1]||r->Reserved[2]||r->Folder[r->PathLength])return 0;
    /* Only absolute native local-volume folder scopes, never a whole volume. */
    if(r->Folder[0]!='\\'||r->Folder[1]!='D'||r->Folder[2]!='e'||r->Folder[3]!='v'||r->Folder[4]!='i'||r->Folder[5]!='c'||r->Folder[6]!='e'||r->Folder[7]!='\\')return 0;
    for(i=0;i<r->PathLength;++i){
        FF_U16 c=r->Folder[i];
        if(!c||c<' '||c=='/'||c=='*'||c=='?'||c==':')return 0;
        if(c=='\\'){
            ++parts;if(i+1==r->PathLength||r->Folder[i+1]=='\\')return 0;
            if(r->Folder[i+1]=='.'&&(i+2==r->PathLength||r->Folder[i+2]=='\\'||(r->Folder[i+2]=='.'&&(i+3==r->PathLength||r->Folder[i+3]=='\\'))))return 0;
        }
    }
    if(parts<3)return 0;
    return 1;
}
typedef FF_U16 (*FF_UPCASE)(FF_U16);
static __inline int FfScope(const FF_RULE* r,const FF_U16* name,unsigned length,FF_UPCASE upper){
    unsigned i;
    if(!name||length<r->PathLength)return 0;
    for(i=0;i<r->PathLength;++i)if(upper(r->Folder[i])!=upper(name[i]))return 0;
    return length==r->PathLength||name[r->PathLength]=='\\'||name[r->PathLength]==':';
}
static __inline unsigned FfSelect(const FF_RULE* rules,const void* const* processes,unsigned count,const void* requestor,unsigned operations,
    const FF_U16* name,unsigned length,const FF_U16* target,unsigned targetLength,FF_UPCASE upper){
    unsigned i;
    if(count>FF_RULE_LIMIT)return FF_RULE_LIMIT;
    for(i=0;i<count;++i)if((rules[i].Operations&operations)&&(!processes[i]||processes[i]==requestor)&&
        (FfScope(&rules[i],name,length,upper)||FfScope(&rules[i],target,targetLength,upper)))return i;
    return FF_RULE_LIMIT;
}
#endif
