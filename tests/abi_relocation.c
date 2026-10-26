/* GPL-3.0-only. x64 C/C++ and short-enum ABI pins. */
#include "sc_relocation.h"
#include <stddef.h>
#include <string.h>
#ifdef __cplusplus
#define PIN(x) static_assert(x,#x)
#else
#define PIN(x) _Static_assert(x,#x)
#endif
PIN(sizeof(sco_api)==128);
PIN(sizeof(sc_relocation_v1)==32);
PIN(offsetof(sc_relocation_v1,size)==0);
PIN(offsetof(sc_relocation_v1,request)==8);
PIN(offsetof(sc_relocation_v1,cancel)==16);
PIN(offsetof(sc_relocation_v1,status)==24);
PIN(sizeof(sc_relocation_request)==72);
PIN(offsetof(sc_relocation_request,size)==0);
PIN(offsetof(sc_relocation_request,timeout_ms)==4);
PIN(offsetof(sc_relocation_request,zone_id)==8);
PIN(offsetof(sc_relocation_request,pos)==16);
PIN(offsetof(sc_relocation_request,rot_xyzw)==40);
PIN(sizeof(sc_relocation_status)==24);
PIN(offsetof(sc_relocation_status,size)==0);
PIN(offsetof(sc_relocation_status,state)==4);
PIN(offsetof(sc_relocation_status,reason)==8);
PIN(offsetof(sc_relocation_status,applied)==12);
PIN(offsetof(sc_relocation_status,ticket)==16);
PIN(SC_RELOCATION_VERSION==0x00010000u);
PIN(SC_RELOCATION_VERSION_1_1==0x00010001u);
PIN(sizeof(sc_relocation_position_request)==48);
PIN(offsetof(sc_relocation_position_request,size)==0);
PIN(offsetof(sc_relocation_position_request,timeout_ms)==4);
PIN(offsetof(sc_relocation_position_request,zone_id)==8);
PIN(offsetof(sc_relocation_position_request,pos)==16);
PIN(offsetof(sc_relocation_position_request,target_epoch)==40);
PIN(sizeof(sc_relocation_v1_1)==40);
PIN(offsetof(sc_relocation_v1_1,request)==8);
PIN(offsetof(sc_relocation_v1_1,cancel)==16);
PIN(offsetof(sc_relocation_v1_1,status)==24);
PIN(offsetof(sc_relocation_v1_1,request_position)==32);
#define HAS(t,m) ((t) && (t)->size>=offsetof(sc_relocation_v1,m)+sizeof((t)->m) && (t)->m)
#ifdef __cplusplus
extern "C" {
#endif
sco_result sco_relocation_route(const sc_relocation_v1* table, unsigned method, sco_plugin* self);
sco_result sco_relocation_route(const sc_relocation_v1* table, unsigned method, sco_plugin* self) {
    sc_relocation_request r; sc_relocation_status s; uint64_t ticket=0;
    memset(&r,0,sizeof(r));memset(&s,0,sizeof(s));
    r.size=sizeof(r);r.zone_id=2;r.timeout_ms=1000;r.rot_xyzw[3]=1;s.size=sizeof(s);
    switch(method) {
    case 0:return HAS(table,request) ? table->request(self,&r,&ticket) : SCO_UNAVAILABLE;
    case 1:return HAS(table,cancel) ? table->cancel(self,1) : SCO_UNAVAILABLE;
    case 2:return HAS(table,status) ? table->status(self,1,&s) : SCO_UNAVAILABLE;
    default:return SCO_BAD_ARG;
    }
}
sco_result sco_relocation_position_route(const sc_relocation_v1_1* table, sco_plugin* self,
                                        const sc_relocation_position_request* request, uint64_t* ticket);
sco_result sco_relocation_position_route(const sc_relocation_v1_1* table, sco_plugin* self,
                                        const sc_relocation_position_request* request, uint64_t* ticket) {
    return table && table->size >= offsetof(sc_relocation_v1_1,request_position)+sizeof(table->request_position) &&
           table->request_position ? table->request_position(self,request,ticket) : SCO_UNAVAILABLE;
}
#ifdef __cplusplus
}
#endif
