// SPDX-License-Identifier: GPL-3.0-only
// Explicitly packaged native open-only peer; no socket, process or plugin invocation.
#include "ipc/diagnostics_ipc.h"
#include <cstring>
#include <new>
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
namespace di=sco::diagnostics::ipc;
EXPORT unsigned sco_rt_adapter_version(){return 1;}
EXPORT uint64_t sco_rt_now(){return di::ClockNow();}
EXPORT void* sco_rt_create(){try{return new di::Peer();}catch(...){return nullptr;}}
EXPORT void sco_rt_destroy(void* p){delete static_cast<di::Peer*>(p);}
EXPORT void sco_rt_close(void* p){if(p)static_cast<di::Peer*>(p)->Close();}
EXPORT int sco_rt_begin(void* p,const char* path){
    try{return int(p&&path?static_cast<di::Peer*>(p)->Begin(path,di::ClockNow()):di::Result::Unavailable);}catch(...){return int(di::Result::Unavailable);}
}
EXPORT int sco_rt_challenge(void* p){try{return int(p?static_cast<di::Peer*>(p)->Challenge(di::ClockNow()):di::Result::Unavailable);}catch(...){return int(di::Result::Unavailable);}}
EXPORT int sco_rt_ready(void* p){try{return int(p?static_cast<di::Peer*>(p)->Ready(di::ClockNow()):di::Result::Unavailable);}catch(...){return int(di::Result::Unavailable);}}
EXPORT int sco_rt_heartbeat(void* p){try{return int(p?static_cast<di::Peer*>(p)->Heartbeat(di::ClockNow()):di::Result::Unavailable);}catch(...){return int(di::Result::Unavailable);}}
EXPORT int sco_rt_send(void* p,unsigned opcode,const char* input,unsigned bytes){
    if(!p||!input||opcode<0x100||opcode>0x105||bytes>3992)return int(di::Result::Corrupt);
    try{return int(static_cast<di::Peer*>(p)->Send(uint16_t(opcode),std::string_view(input,bytes),di::ClockNow()));}catch(...){return int(di::Result::Unavailable);}
}
EXPORT int sco_rt_receive(void* p,char* output,unsigned capacity,unsigned* bytes){
    if(bytes)*bytes=0;if(!p||!output||!bytes||capacity!=65536)return int(di::Result::Corrupt);
    try{std::string text;auto r=static_cast<di::Peer*>(p)->Receive(di::ClockNow(),text);
        if(r!=di::Result::Ok)return int(r);if(text.size()>capacity)return int(di::Result::Corrupt);
        std::memcpy(output,text.data(),text.size());*bytes=unsigned(text.size());return int(di::Result::Ok);
    }catch(...){return int(di::Result::Unavailable);}
}
