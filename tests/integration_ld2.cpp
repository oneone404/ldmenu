#include "../backend.h"
#include <cstdio>
#include <algorithm>
using namespace ldcontrol;
int wmain(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Local\\NPHReconnectConsole-20261023");
    if(!mutex||GetLastError()==ERROR_ALREADY_EXISTS){std::puts("OTHER_CONTROLLER_RUNNING");return 3;}
    Engine engine;engine.start(nullptr);int stage=-1;DWORD firstPid=0;auto deadline=GetTickCount64()+600000;unsigned long long lastGeneration=0;std::wstring previous;
    for(;GetTickCount64()<deadline;Sleep(250)){
        auto state=engine.snapshot();auto it=std::find_if(state.rows.begin(),state.rows.end(),[](auto&r){return r.index==2&&r.name==L"LD-2"&&r.running;});
        if(it==state.rows.end())continue;
        if(it->detail!=previous){previous=it->detail;int n=WideCharToMultiByte(CP_UTF8,0,previous.data(),int(previous.size()),nullptr,0,nullptr,nullptr);std::string output(n,'\0');WideCharToMultiByte(CP_UTF8,0,previous.data(),int(previous.size()),output.data(),n,nullptr,nullptr);std::printf("STATE %s\n",output.c_str());}
        if(stage<0){Config cfg;cfg.function=2;cfg.filter=1;cfg.reconnect=true;std::wstring error;if(!engine.configure(2,L"LD-2",cfg,error)){std::puts("SAVE_FAILED");return 3;}engine.toggle(2);stage=0;std::puts("CONNECT_STARTED_LD2");}
        if(it->phase==Phase::Failed){std::printf("FAILED stage=%d\n",stage);std::fwprintf(stderr,L"%ls\n",it->detail.c_str());return 7;}
        if(it->phase==Phase::Connected&&(it->detail==L"Đã áp dụng cấu hình"||it->detail==L"Đã nhận lại cấu hình đang chạy")){
            if(stage==0){firstPid=it->game;lastGeneration=it->generation;std::printf("APPLY_PASS_LD2 pid=%lu\n",firstPid);stage=1;}
            else if(stage==1&&it->game!=firstPid){std::printf("RECONNECT_AND_APPLY_PASS_LD2 pid=%lu sameGeneration=%d\n",it->game,it->generation==lastGeneration);Config off;std::wstring error;if(!engine.configure(2,L"LD-2",off,error))return 7;stage=2;}
            else if(stage==2&&it->config.function==0&&it->config.filter==0&&!it->config.sell&&!it->applyAt){engine.toggle(2);std::puts("RESTORED_OFF_PASS");engine.stop();ReleaseMutex(mutex);CloseHandle(mutex);return 0;}
        }
    }
    std::puts("INTEGRATION_TIMEOUT");return 7;
}

