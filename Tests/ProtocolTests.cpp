#include "../SDK/FileFilter.h"
#include "../Installer/Setup.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
#include <filesystem>
static void Check(bool value){if(!value)throw std::runtime_error("File-filter offline regression failed.");}
int main(){try{
    auto rule=FF_RULE{};rule.Id=1;rule.Operations=FF_ALL;std::wstring name=L"\\Device\\HarddiskVolume3\\Example";rule.PathLength=static_cast<FF_U32>(name.size());std::copy(name.begin(),name.end(),rule.Folder);Check(FileFilter::Validate(rule));
    for(unsigned n=FF_PATH_LIMIT;n<FF_PATH_LIMIT+100;++n){auto bad=rule;bad.PathLength=n;Check(!FileFilter::Validate(bad));}
    auto bad=rule;bad.Reserved[2]=1;Check(!FileFilter::Validate(bad));bad=rule;bad.ProcessId=4;Check(!FileFilter::Validate(bad));bad=rule;bad.Operations=256;Check(!FileFilter::Validate(bad));
    auto policy=std::make_unique<FF_POLICY>();policy->Header={FF_MAGIC,FF_VERSION,sizeof(FF_POLICY),FF_COMMAND_POLICY,2,0};policy->Rules[0]=policy->Rules[1]=rule;Check(!FileFilter::Validate(*policy));policy->Rules[1].Id=2;Check(FileFilter::Validate(*policy));policy->Header.Count=33;Check(!FileFilter::Validate(*policy));
    auto reply=std::make_unique<FF_REPLY>();auto& s=reply->Status;s={FF_MAGIC,FF_VERSION,sizeof(FF_STATUS),0,0,0,FF_RING_CAPACITY,0,1,10000000,0,0,0,0,0,1};Check(FileFilter::Validate(*reply,sizeof(FF_STATUS)));
    s.Count=1;s.RuleCount=1;s.Size=sizeof(FF_STATUS)+sizeof(FF_EVENT);s.NextSequence=2;s.Observed=s.Denied=1;auto& e=reply->Events[0];e.Sequence=e.Generation=e.StartQpc=e.RuleId=1;e.Operations=FF_WRITE;e.Status=0xC0000022u;e.Flags=FF_FLAG_DENIED;e.PathLength=static_cast<FF_U16>(name.size());std::copy(name.begin(),name.end(),e.Path);Check(FileFilter::Validate(*reply,s.Size));
    e.DurationQpc=1;Check(!FileFilter::Validate(*reply,s.Size));e.DurationQpc=0;e.TargetLength=FF_PATH_LIMIT;Check(!FileFilter::Validate(*reply,s.Size));e.TargetLength=0;
    std::minstd_rand random(904211);for(unsigned i=0;i<20000;++i){auto previous=s.Magic;s.Magic=static_cast<FF_U32>(random());if(s.Magic!=FF_MAGIC)Check(!FileFilter::Validate(*reply,s.Size));s.Magic=previous;}
    Check(!Setup::CatalogTrusted(L"This-file-does-not-exist.cat"));Check(!Setup::PackageMatches(L"This-file-does-not-exist.inf"));
    wchar_t temp[MAX_PATH]{};Check(GetTempPathW(MAX_PATH,temp)>0);GUID id{};Check(SUCCEEDED(CoCreateGuid(&id)));wchar_t token[40]{};StringFromGUID2(id,token,40);auto path=std::filesystem::path(temp)/(std::wstring(L"TitanFileFilterOffline-")+token);Check(CreateDirectoryW(path.c_str(),nullptr)!=FALSE);
    try{
        auto resolved=FileFilter::Rule(1,path.wstring(),FF_WRITE,FF_DENY,GetCurrentProcessId());Check(FileFilter::Validate(resolved));Check(FileFilter::Folder(resolved).find(L"\\Device\\HarddiskVolume")==0);
        bool refused=false;try{FileFilter::Rule(1,(path/L"missing").wstring());}catch(const std::exception&){refused=true;}Check(refused);
    }catch(...){RemoveDirectoryW(path.c_str());throw;}Check(RemoveDirectoryW(path.c_str())!=FALSE);
    std::cout<<"Offline wire validation, bounded policy, directory normalization and missing/untrusted-package checks passed. No driver was loaded or contacted.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
