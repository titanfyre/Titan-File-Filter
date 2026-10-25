#include "../SDK/FileFilter.h"
#include <windows.h>
#include <iostream>
int wmain(int argc,wchar_t** argv){
    std::cout<<"Temporarily deny this process's writes/deletes in a selected existing folder. Enter restores access.\n";
    if(argc!=2){std::cout<<"Usage: FileFilterExample.exe C:\\ExampleFolder\n";return 2;}
    try{
        FileFilter::Session files;
        files.Start({FileFilter::Rule(1,argv[1],FF_WRITE|FF_DELETE,FF_DENY,GetCurrentProcessId())});
        std::cin.get();
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
