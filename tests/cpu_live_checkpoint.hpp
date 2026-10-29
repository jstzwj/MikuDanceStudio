#pragma once
// The GUI test owns these requests. The application consumes them only at a
// normal scene-clear boundary and acknowledges after all image/lifecycle gates.
inline bool CpuLiveCheckpoint(HWND main,DWORD process,const char* phase){
    const auto* live=_wgetenv(L"MDS_RHI_CPU_LIVE_HISTORY");if(!live || wcscmp(live,L"1"))return true;
    const auto* directory=_wgetenv(L"MDS_RHI_CPU_GEOMETRY_DIR");if(!directory)return false;
    static std::map<DWORD,unsigned> checkpoints;const unsigned id=++checkpoints[process];
    const auto stem=std::filesystem::path(directory)/("cpu_live.request."+std::to_string(process)+".txt");
    const auto temporary=std::filesystem::path(stem.wstring()+L".tmp");
    {std::ofstream request(temporary);request<<id<<" "<<phase<<"\n";if(!request)return false;}
    if(!MoveFileExW(temporary.c_str(),stem.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return false;
    const auto ack=std::filesystem::path(directory)/("cpu_live.ack."+std::to_string(process)+".txt");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
    while(std::chrono::steady_clock::now()<deadline){
        std::ifstream answer(ack);unsigned completed=0;std::string status;
        if(answer>>completed>>status && completed==id){std::printf("CPU_LIVE pid=%lu checkpoint=%u phase=%s status=%s\n",process,id,phase,status.c_str());std::fflush(stdout);
            return status=="PASS";}
        if(!IsWindow(main))return false;
        // Invalid regions coalesce; posting WM_PAINT every poll would queue
        // hundreds of messages while GPU readback blocks the application.
        // Those messages can delay play/stop and the following Save As command.
        InvalidateRect(main,nullptr,FALSE);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::fprintf(stderr,"CPU_LIVE timeout pid=%lu checkpoint=%u phase=%s\n",process,id,phase);return false;
}
