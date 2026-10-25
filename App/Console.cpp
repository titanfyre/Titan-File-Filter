#include "../SDK/FileFilter.h"
#include <windows.h>
#include <iostream>
int wmain(int argc,wchar_t** argv){
    if(argc==2&&std::wstring(argv[1])==L"--preview"){
        std::cout<<"OFFLINE SAMPLE: PID 4242 | Write | document.txt | Denied | rule 1\nNo driver connection, capture, policy or installer was used.\n";return 0;
    }
    if(argc!=3){std::cout<<"FileFilterCLI.exe EXISTING_FOLDER SECONDS\nAudits supported operations; does not block them. Run as administrator after driver allocation/signing/validation.\n";return 2;}
    try{
        auto seconds=std::stoul(argv[2]);if(!seconds||seconds>3600)throw std::invalid_argument("Duration must be 1 to 3600 seconds.");
        FileFilter::Session files;files.Start({FileFilter::Rule(1,argv[1])});const auto end=GetTickCount64()+seconds*1000ull;
        std::wcout<<L"Explicit local file metadata audit. Ctrl+C stops capture.\n";
        while(GetTickCount64()<end){for(const auto& e:files.Read())std::wcout<<e.ProcessId<<L" | "<<FileFilter::Operations(e.Operations)<<L" | "<<FileFilter::Path(e)<<L" | NTSTATUS "<<std::hex<<e.Status<<std::dec<<L'\n';Sleep(20);}
        const auto status=files.Status();std::wcout<<L"Observed "<<status.Observed<<L", dropped "<<status.Dropped<<L", unresolved names "<<status.NameFailures<<L", skipped "<<status.Skipped<<L'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
