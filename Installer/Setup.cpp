#include "Setup.h"
#include <setupapi.h>
#include <fltuser.h>
#include <newdev.h>
#include <devguid.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <mscat.h>
#include <sddl.h>
#include <objbase.h>
#include <vector>
#include <filesystem>
#include <stdexcept>
#include <set>
namespace fs=std::filesystem;
namespace {
std::wstring Message(DWORD e){wchar_t* s=nullptr;FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,e,0,reinterpret_cast<LPWSTR>(&s),0,nullptr);std::wstring r=s?s:L"Operation failed";if(s)LocalFree(s);return r+L" (Windows error "+std::to_wstring(e)+L")";}
void Require(bool ok){if(!ok)throw std::system_error(static_cast<int>(GetLastError()?GetLastError():ERROR_GEN_FAILURE),std::system_category());}
bool MicrosoftCatalog(const fs::path& path){
    WINTRUST_FILE_INFO file{};file.cbStruct=sizeof(file);file.pcwszFilePath=path.c_str();
    WINTRUST_DATA data{};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;LONG status=WinVerifyTrust(nullptr,&action,&data);bool accepted=false;
    if(status==ERROR_SUCCESS){auto provider=WTHelperProvDataFromStateData(data.hWVTStateData);auto signer=provider?WTHelperGetProvSignerFromChain(provider,0,FALSE,0):nullptr;if(signer&&signer->csCertChain){wchar_t name[512]{};CertGetNameStringW(signer->pasCertChain[0].pCert,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,nullptr,name,512);accepted=_wcsicmp(name,L"Microsoft Windows Hardware Compatibility Publisher")==0;}}
    data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);return accepted;
}
bool CatalogMember(const fs::path& directory,const wchar_t* name){
    const auto path=directory/name;const auto catalog=directory/L"TitanFileFilter.cat";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    struct File{HANDLE h;~File(){CloseHandle(h);}} close{file};
    HCATADMIN admin=nullptr;if(!CryptCATAdminAcquireContext2(&admin,nullptr,L"SHA256",nullptr,0))return false;
    struct Admin{HCATADMIN h;~Admin(){CryptCATAdminReleaseContext(h,0);}} release{admin};
    BYTE hash[64]{};DWORD bytes=sizeof(hash);if(!CryptCATAdminCalcHashFromFileHandle2(admin,file,&bytes,hash,0)||bytes>64)return false;
    wchar_t tag[129]{};const wchar_t* hex=L"0123456789ABCDEF";for(DWORD i=0;i<bytes;++i){tag[i*2]=hex[hash[i]>>4];tag[i*2+1]=hex[hash[i]&15];}
    WINTRUST_CATALOG_INFO member{};member.cbStruct=sizeof(member);member.pcwszCatalogFilePath=catalog.c_str();member.pcwszMemberTag=tag;member.pcwszMemberFilePath=path.c_str();member.hMemberFile=file;member.pbCalculatedFileHash=hash;member.cbCalculatedFileHash=bytes;member.hCatAdmin=admin;
    WINTRUST_DATA data{};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.dwUnionChoice=WTD_CHOICE_CATALOG;data.pCatalog=&member;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL|WTD_REVOCATION_CHECK_NONE;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;LONG result=WinVerifyTrust(nullptr,&action,&data);data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);return result==ERROR_SUCCESS;
}
bool OwnPackage(const fs::path& path){
 GUID classId{};wchar_t className[128]{};const GUID expected={0xd02bc3da,0x0c8e,0x4945,{0x9b,0xd5,0xf1,0x88,0x3c,0x22,0x6c,0x8c}};
 if(!SetupDiGetINFClassW(path.c_str(),&classId,className,128,nullptr)||!IsEqualGUID(classId,expected))return false;
 HINF inf=SetupOpenInfFileW(path.c_str(),nullptr,INF_STYLE_WIN4,nullptr);if(inf==INVALID_HANDLE_VALUE)return false;
 struct Close{HINF h;~Close(){SetupCloseInfFile(h);}} close{inf};INFCONTEXT line{};wchar_t value[512]{};
 const auto field=[&](const wchar_t* section,const wchar_t* key,const wchar_t* expectedValue){return SetupFindFirstLineW(inf,section,key,&line)&&SetupGetStringFieldW(&line,1,value,512,nullptr)&&!_wcsicmp(value,expectedValue);};
 bool altitude=false;
 if(SetupFindFirstLineW(inf,L"Settings",nullptr,&line))do{
  wchar_t name[128]{},number[80]{};
  if(SetupGetStringFieldW(&line,3,name,128,nullptr)&&!_wcsicmp(name,L"Altitude")&&SetupGetStringFieldW(&line,5,number,80,nullptr)){
   unsigned integer=0,digits=0;bool valid=true,fraction=false;for(const wchar_t* p=number;*p;++p){if(*p==L'.'&&!fraction&&digits==5){fraction=true;digits=0;continue;}if(*p<L'0'||*p>L'9'){valid=false;break;}if(!fraction){if(++digits>5){valid=false;break;}integer=integer*10+static_cast<unsigned>(*p-L'0');}else ++digits;}
   if(valid&&digits&&integer>=80000&&integer<=89999)altitude=true;else return false;
  }
 }while(SetupFindNextLine(&line,&line));
 return altitude&&field(L"Version",L"CatalogFile",L"TitanFileFilter.cat")&&field(L"Version",L"Provider",L"Titanfyre")&&
  field(L"DefaultInstall.NTamd64.10.0...19041.Services",L"AddService",L"TitanFileFilter")&&field(L"Service",L"ServiceType",L"2")&&field(L"Service",L"StartType",L"3")&&field(L"Service",L"Dependencies",L"FltMgr")&&field(L"Service",L"LoadOrderGroup",L"FSFilter Security Enhancer");
}
struct Temporary {
    fs::path directory;
    bool owned=false;
    ~Temporary(){if(!owned)return;for(auto name:{L"TitanFileFilter.inf",L"TitanFileFilter.cat",L"TitanFileFilter.sys"})DeleteFileW((directory/name).c_str());RemoveDirectoryW(directory.c_str());}
    void Create(){
        wchar_t windows[MAX_PATH]{};DWORD length=GetWindowsDirectoryW(windows,MAX_PATH);Require(length>0&&length<MAX_PATH);GUID guid{};Require(SUCCEEDED(CoCreateGuid(&guid)));wchar_t id[40]{};StringFromGUID2(guid,id,40);
        // Use a system-owned parent, not the initiating user's writable Temp tree.
        directory=fs::path(windows)/L"Temp"/(std::wstring(L"TitanFileFilter-")+id);
        PSECURITY_DESCRIPTOR descriptor=nullptr;Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&descriptor,nullptr)!=FALSE);
        SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};bool made=CreateDirectoryW(directory.c_str(),&security)!=FALSE;DWORD error=GetLastError();LocalFree(descriptor);if(!made){SetLastError(error);Require(false);}
        owned=true;
        const wchar_t* names[]{L"TitanFileFilter.inf",L"TitanFileFilter.cat",L"TitanFileFilter.sys"};
        for(int i=0;i<3;++i){
            HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(101+i),RT_RCDATA);Require(resource!=nullptr);HGLOBAL loaded=LoadResource(nullptr,resource);Require(loaded!=nullptr);DWORD size=SizeofResource(nullptr,resource);void* bytes=LockResource(loaded);Require(bytes&&size&&size<16*1024*1024);
            HANDLE f=CreateFileW((directory/names[i]).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);Require(f!=INVALID_HANDLE_VALUE);DWORD written=0;bool ok=WriteFile(f,bytes,size,&written,nullptr)&&written==size;error=GetLastError();CloseHandle(f);if(!ok){SetLastError(error);Require(false);}
        }
    }
};
struct Service {
 SC_HANDLE manager=nullptr,service=nullptr;
 Service(DWORD access){manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);Require(manager!=nullptr);service=OpenServiceW(manager,L"TitanFileFilter",access);if(!service&&GetLastError()!=ERROR_SERVICE_DOES_NOT_EXIST){auto error=GetLastError();CloseServiceHandle(manager);manager=nullptr;SetLastError(error);Require(false);}}
 ~Service(){if(service)CloseServiceHandle(service);if(manager)CloseServiceHandle(manager);}
};
DWORD State(){Service service(SERVICE_QUERY_STATUS);if(!service.service)return SERVICE_STOPPED;SERVICE_STATUS_PROCESS status{};DWORD bytes=0;Require(QueryServiceStatusEx(service.service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&bytes)!=FALSE);return status.dwCurrentState;}
struct LoadPrivilege {
 HANDLE token=nullptr;TOKEN_PRIVILEGES previous{};bool changed=false;
 LoadPrivilege(){Require(OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token)!=FALSE);TOKEN_PRIVILEGES next{};next.PrivilegeCount=1;if(!LookupPrivilegeValueW(nullptr,SE_LOAD_DRIVER_NAME,&next.Privileges[0].Luid)){auto error=GetLastError();CloseHandle(token);token=nullptr;SetLastError(error);Require(false);}next.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;DWORD bytes=sizeof(previous);SetLastError(ERROR_SUCCESS);changed=AdjustTokenPrivileges(token,FALSE,&next,sizeof(previous),&previous,&bytes)!=FALSE;DWORD error=GetLastError();if(!changed||error){CloseHandle(token);token=nullptr;SetLastError(error);Require(false);}}
 ~LoadPrivilege(){if(token){if(changed)AdjustTokenPrivileges(token,FALSE,&previous,0,nullptr,nullptr);CloseHandle(token);}}
};
Setup::Result Install(){
 Temporary temp;temp.Create();
 if(!OwnPackage(temp.directory/L"TitanFileFilter.inf"))return {ERROR_INVALID_DATA,false,L"The embedded package does not match the exact Titan File Filter identity."};
 if(!MicrosoftCatalog(temp.directory/L"TitanFileFilter.cat")||!CatalogMember(temp.directory,L"TitanFileFilter.inf")||!CatalogMember(temp.directory,L"TitanFileFilter.sys"))return {ERROR_INVALID_IMAGE_HASH,false,L"The driver requires a trusted Microsoft catalog and matching INF/SYS members. No unsigned fallback is permitted."};
 BOOL restart=FALSE;
 if(!DiInstallDriverW(nullptr,(temp.directory/L"TitanFileFilter.inf").c_str(),0,&restart)){auto error=GetLastError();return {error,restart!=FALSE,L"Windows refused primitive minifilter installation. "+Message(error)};}
 LoadPrivilege privilege;HRESULT result=FilterLoad(L"TitanFileFilter");
 if(FAILED(result)&&State()!=SERVICE_RUNNING)return {static_cast<DWORD>(result),restart!=FALSE,L"The package was installed, but Filter Manager could not load it. Review its altitude, signing and Windows status. "+Message(static_cast<DWORD>(result))};
 return {0,restart!=FALSE,L"Titan File Filter installed. No access policy is active until a client explicitly starts a scoped session."};
}
Setup::Result Uninstall(){
 if(State()==SERVICE_RUNNING){LoadPrivilege privilege;HRESULT result=FilterUnload(L"TitanFileFilter");if(FAILED(result))return {static_cast<DWORD>(result),false,L"Filter Manager could not unload this filter. No package was removed. "+Message(static_cast<DWORD>(result))};}
 wchar_t windows[MAX_PATH]{};DWORD length=GetWindowsDirectoryW(windows,MAX_PATH);Require(length>0&&length<MAX_PATH);
 fs::path directory=fs::path(windows)/L"INF";WIN32_FIND_DATAW entry{};HANDLE search=FindFirstFileW((directory/L"oem*.inf").c_str(),&entry);std::vector<fs::path> packages;
 if(search!=INVALID_HANDLE_VALUE){struct Close{HANDLE h;~Close(){FindClose(h);}} close{search};unsigned scanned=0;do{if(++scanned>16384){SetLastError(ERROR_BUFFER_OVERFLOW);Require(false);}if(OwnPackage(directory/entry.cFileName))packages.push_back(directory/entry.cFileName);}while(FindNextFileW(search,&entry));if(GetLastError()!=ERROR_NO_MORE_FILES)Require(false);}
 else if(GetLastError()!=ERROR_FILE_NOT_FOUND)Require(false);
 bool restart=false;
 for(const auto& package:packages){BOOL need=FALSE;if(!DiUninstallDriverW(nullptr,package.c_str(),0,&need)){auto error=GetLastError();return {error,restart,L"The filter is stopped, but Windows retained its driver package. "+Message(error)};}restart=restart||need!=FALSE;}
 return {0,restart,packages.empty()?L"No Titan File Filter package was installed.":L"Titan File Filter uninstalled. Other filters were preserved."};
}
}
unsigned Setup::Installed(DWORD* error){if(error)*error=0;try{Service service(SERVICE_QUERY_STATUS);return service.service?1u:0u;}catch(const std::system_error& e){if(error)*error=static_cast<DWORD>(e.code().value());return 0;}catch(...){if(error)*error=ERROR_GEN_FAILURE;return 0;}}
bool Setup::PackageMatches(const std::wstring& path) noexcept{try{return OwnPackage(fs::path(path));}catch(...){return false;}}
bool Setup::CatalogTrusted(const std::wstring& path) noexcept{try{return MicrosoftCatalog(fs::path(path));}catch(...){return false;}}
Setup::Result Setup::Execute(bool install) noexcept {
    HANDLE mutex=nullptr;
    try {
        mutex=CreateMutexW(nullptr,FALSE,L"Global\\TitanFileFilterSetup");if(!mutex)return {GetLastError(),false,L"Could not open the installer lock."};
        DWORD wait=WaitForSingleObject(mutex,0);if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){CloseHandle(mutex);return {ERROR_BUSY,false,L"Another file filter installation is already running."};}
        Result result=install?Install():Uninstall();ReleaseMutex(mutex);CloseHandle(mutex);return result;
    }catch(const std::system_error& e){if(mutex){ReleaseMutex(mutex);CloseHandle(mutex);}return {static_cast<DWORD>(e.code().value()),false,Message(static_cast<DWORD>(e.code().value()))};}
    catch(...){if(mutex){ReleaseMutex(mutex);CloseHandle(mutex);}return {ERROR_GEN_FAILURE,false,L"The operation failed before completion. No system restart was requested."};}
}
bool Setup::Restart() noexcept {
    HANDLE token{};if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token))return false;TOKEN_PRIVILEGES p{};p.PrivilegeCount=1;bool ok=LookupPrivilegeValueW(nullptr,SE_SHUTDOWN_NAME,&p.Privileges[0].Luid)!=FALSE;p.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
    if(ok){SetLastError(0);ok=AdjustTokenPrivileges(token,FALSE,&p,0,nullptr,nullptr)&&GetLastError()==ERROR_SUCCESS;}CloseHandle(token);
    return ok&&ExitWindowsEx(EWX_REBOOT,SHTDN_REASON_MAJOR_APPLICATION|SHTDN_REASON_MINOR_INSTALLATION|SHTDN_REASON_FLAG_PLANNED);
}
