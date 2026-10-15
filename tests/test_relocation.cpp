// GPL-3.0-only. Synthetic game-provider fixture; no engine/network access.
#include "game/services/relocation_model.h"
// Assertions perform fixture operations and must remain active in Release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <limits>
using namespace sco::game::services::relocation;
static Machine m;
static bool thread=true,ready=true;
static Evidence e{true,true,true,9,11,7,7,true,false,false,false};
static uint64_t now=10;
static sco_result Enter(sco_plugin* self) { return !thread ? SCO_WRONG_THREAD : !self ? SCO_BAD_ARG : !ready ? SCO_UNAVAILABLE : SCO_OK; }
static sco_result Request(sco_plugin* self,const sc_relocation_request* r,uint64_t* ticket) {
    auto result=Enter(self); if(result!=SCO_OK) return result;
    return r ? m.Request(self,*r,e,now,ticket) : SCO_BAD_ARG;
}
static sco_result Cancel(sco_plugin* self,uint64_t ticket) { auto result=Enter(self);return result==SCO_OK ? m.Cancel(self,ticket) : result; }
static sco_result Status(sco_plugin* self,uint64_t ticket,sc_relocation_status* out) { auto result=Enter(self);return result==SCO_OK ? m.Status(self,ticket,out) : result; }
static const sc_relocation_v1 table{sizeof(table),Request,Cancel,Status};
extern "C" sco_result sco_relocation_route(const sc_relocation_v1*,unsigned,sco_plugin*);
#define EXTENT(member) (offsetof(sc_relocation_v1,member)+sizeof(table.member))
template<class T> bool Available(const sc_relocation_v1* t,size_t extent,T sc_relocation_v1::*member) {
    return t && t->size>=extent && t->*member;
}
struct FakeZone { uintptr_t parent; const char* name; uint64_t id; };
static FakeZone fakeZones[8];
static bool zonesStreamedOut=false;
static uintptr_t FakeParent(uintptr_t z) { return fakeZones[z].parent; }
static const char* FakeName(uintptr_t z) { return fakeZones[z].name; }
static uint64_t FakeId(uintptr_t z) { return fakeZones[z].id; }
static uintptr_t FakeFromId(uint64_t id) {
    if(zonesStreamedOut) return 0;
    for(uintptr_t i=1;i<8;++i) if(fakeZones[i].id==id) return i;
    return 0;
}
static bool QuatIs(const double q[4],double x,double y,double z,double w) {
    for(double sign:{1.0,-1.0}) {
        if(std::abs(q[0]-sign*x)<1e-9 && std::abs(q[1]-sign*y)<1e-9 && std::abs(q[2]-sign*z)<1e-9 && std::abs(q[3]-sign*w)<1e-9) return true;
    }
    return false;
}
int main() {
    auto* owner=reinterpret_cast<sco_plugin*>(1);auto* foreign=reinterpret_cast<sco_plugin*>(2);
    sc_relocation_request r{};r.size=sizeof(r);r.zone_id=2;r.rot_xyzw[3]=2;r.timeout_ms=1000;
    uint64_t ticket=44;
    assert(!Available(nullptr,EXTENT(request),&sc_relocation_v1::request));
    for(uint32_t extent:{uint32_t(EXTENT(request)),uint32_t(EXTENT(cancel)),uint32_t(EXTENT(status))}) {
        auto* memory=new char[extent-1]{}; auto* partial=reinterpret_cast<sc_relocation_v1*>(memory);
        partial->size=extent-1;
        if(extent==EXTENT(request)) assert(!Available(partial,extent,&sc_relocation_v1::request));
        if(extent==EXTENT(cancel)) assert(!Available(partial,extent,&sc_relocation_v1::cancel));
        if(extent==EXTENT(status)) assert(!Available(partial,extent,&sc_relocation_v1::status));
        const unsigned method=unsigned(extent/8)-2;
        assert(sco_relocation_route(partial,method,owner)==SCO_UNAVAILABLE);
        delete[] memory;
    }
    const sc_relocation_v1 nulls{sizeof(nulls),nullptr,nullptr,nullptr};
    assert(!Available(&nulls,EXTENT(request),&sc_relocation_v1::request));
    assert(!Available(&nulls,EXTENT(cancel),&sc_relocation_v1::cancel));
    assert(!Available(&nulls,EXTENT(status),&sc_relocation_v1::status));
    sc_relocation_status s{};s.size=sizeof(s);s.state=99;
    thread=false;
    assert(table.request(owner,&r,&ticket)==SCO_WRONG_THREAD && ticket==44);
    assert(table.cancel(owner,1)==SCO_WRONG_THREAD);
    assert(table.status(owner,1,&s)==SCO_WRONG_THREAD && s.state==99);thread=true;
    ready=false;assert(table.request(owner,&r,&ticket)==SCO_UNAVAILABLE);ready=true;
    assert(table.request(nullptr,&r,&ticket)==SCO_BAD_ARG);
    assert(table.request(owner,nullptr,&ticket)==SCO_BAD_ARG);
    assert(table.request(owner,&r,nullptr)==SCO_BAD_ARG);
    for(int variant=0;variant<7;++variant) {
        auto bad=r;
        if(variant==0) bad.size=sizeof(bad)-1;
        if(variant==1) bad.zone_id=0;
        if(variant==2) bad.timeout_ms=0;
        if(variant==3) bad.timeout_ms=60001;
        if(variant==4) bad.pos[0]=std::numeric_limits<double>::infinity();
        if(variant==5) bad.rot_xyzw[3]=0;
        if(variant==6) bad.rot_xyzw[0]=std::numeric_limits<double>::quiet_NaN();
        assert(table.request(owner,&bad,&ticket)==SCO_BAD_ARG && ticket==44);
    }
    for(int variant=0;variant<9;++variant) {
        auto save=e;
        if(variant==0) e.offline=false;
        if(variant==1) e.transport=false;
        if(variant==2) e.player=false;
        if(variant==3) e.session=0;
        if(variant==4) e.currentSystem=0;
        if(variant==5) e.targetSystem=8;
        if(variant==6) e.targetSystem=0;
        if(variant==7) e.streamed=false;
        if(variant==8) e.fault=true;
        assert(table.request(owner,&r,&ticket)==SCO_UNAVAILABLE && ticket==44);e=save;
    }
    assert(table.request(owner,&r,&ticket)==SCO_OK && ticket);
    assert(m.Pending()->rot_xyzw[3]==1);
    uint64_t unchanged=987;assert(table.request(foreign,&r,&unchanged)==SCO_TOO_MANY && unchanged==987);
    assert(table.status(foreign,ticket,&s)==SCO_NOT_FOUND && s.state==99);
    assert(table.cancel(foreign,ticket)==SCO_NOT_FOUND);
    assert(table.cancel(owner,0)==SCO_BAD_ARG);
    assert(table.status(owner,ticket,nullptr)==SCO_BAD_ARG);
    s.size=sizeof(s)-1;assert(table.status(owner,ticket,&s)==SCO_BAD_ARG && s.state==99);s.size=sizeof(s);
    assert(table.status(owner,ticket,&s)==SCO_OK && s.state==SC_RELOCATION_PENDING);
    e.streamed=false;assert(!m.Tick(20,e));e.streamed=true;
    assert(m.Tick(30,e));m.Applied(true);assert(!m.Tick(40,e));
    assert(table.status(owner,ticket,&s)==SCO_OK && s.state==SC_RELOCATION_PENDING && s.applied);
    e.arrived=true;assert(!m.Tick(50,e));e.arrived=false;
    assert(table.status(owner,ticket,&s)==SCO_OK && s.state==SC_RELOCATION_COMPLETE);
    assert(table.cancel(owner,ticket)==SCO_OK);
    for(int variant=0;variant<8;++variant) {
        assert(table.request(owner,&r,&ticket)==SCO_OK);auto save=e;
        uint32_t reason=0;uint64_t tick=20;
        if(variant==0) { e.offline=false;reason=SC_RELOCATION_REASON_SAFETY; }
        if(variant==1) { e.session=10;reason=SC_RELOCATION_REASON_SESSION; }
        if(variant==2) { e.playerId=12;reason=SC_RELOCATION_REASON_SESSION; }
        if(variant==3) { e.currentSystem=8;reason=SC_RELOCATION_REASON_SYSTEM; }
        if(variant==4) { e.targetSystem=8;reason=SC_RELOCATION_REASON_SYSTEM; }
        if(variant==5) { e.transport=false;reason=SC_RELOCATION_REASON_TRANSPORT; }
        if(variant==6) { e.fault=true;reason=SC_RELOCATION_REASON_FAULT; }
        if(variant==7) { e.streamed=false;tick=1010;reason=SC_RELOCATION_REASON_TIMEOUT; }
        assert(!m.Tick(tick,e));assert(table.status(owner,ticket,&s)==SCO_OK && s.state==SC_RELOCATION_FAILED && s.reason==reason);e=save;
    }
    assert(table.request(owner,&r,&ticket)==SCO_OK);assert(table.cancel(owner,ticket)==SCO_OK);
    assert(table.cancel(owner,ticket)==SCO_OK && !m.Tick(20,e));
    assert(table.status(owner,ticket,&s)==SCO_OK && s.state==SC_RELOCATION_CANCELLED);
    assert(table.request(owner,&r,&ticket)==SCO_OK);m.Applied(false);
    assert(table.status(owner,ticket,&s)==SCO_OK && s.reason==SC_RELOCATION_REASON_TRANSPORT);
    assert(table.request(owner,&r,&ticket)==SCO_OK);m.Release(owner);
    assert(table.status(owner,ticket,&s)==SCO_NOT_FOUND);
    uint64_t old=ticket;assert(table.request(owner,&r,&ticket)==SCO_OK && ticket>old);m.Stop();
    assert(table.status(owner,ticket,&s)==SCO_NOT_FOUND);
    double p[3]={},q[4]={0,0,0,-1};assert(Arrived(r,p,q));
    p[0]=0.051;assert(!Arrived(r,p,q));p[0]=0;q[0]=1;q[3]=0;assert(!Arrived(r,p,q));
    for(unsigned method=0;method<3;++method) assert(sco_relocation_route(&nulls,method,owner)==SCO_UNAVAILABLE);
    assert(sco_relocation_route(&table,0,owner)==SCO_OK);
    assert(sco_relocation_route(&table,1,owner)==SCO_NOT_FOUND);
    assert(sco_relocation_route(&table,2,owner)==SCO_NOT_FOUND);
    {
    const ZoneOps ops{FakeParent,FakeName,FakeId,FakeFromId};
    fakeZones[1]={2,"Daymar",10}; fakeZones[2]={3,"OOC_Stanton_2b",11};
    fakeZones[3]={4,"solarSYSTEM_Stanton",12}; fakeZones[4]={0,"Universe",13};
    assert(SystemOfZone(1,ops)==12 && SystemOfZone(3,ops)==12);
    assert(SystemOfZone(4,ops)==0 && SystemOfZone(0,ops)==0);
    zonesStreamedOut=true;assert(SystemOfZone(1,ops)==0);zonesStreamedOut=false;
    fakeZones[3].id=0;assert(SystemOfZone(1,ops)==0);fakeZones[3].id=12;
    fakeZones[3].name="Solar";assert(SystemOfZone(1,ops)==0);fakeZones[3].name=nullptr;assert(SystemOfZone(1,ops)==0);
    fakeZones[3].name="SolarSystem_Stanton";assert(SystemOfZone(1,ops)==12);
    fakeZones[5]={6,"A",20}; fakeZones[6]={5,"B",21};
    assert(SystemOfZone(5,ops)==0);
    double axes[3][3]={{1,0,0},{0,1,0},{0,0,1}},qa[4],w[4];
    assert(QuatFromAxes(axes,qa) && QuatIs(qa,0,0,0,1));
    const double rt=std::sqrt(0.5);
    const double quarterZ[3][3]={{0,1,0},{-1,0,0},{0,0,1}};
    assert(QuatFromAxes(quarterZ,qa) && QuatIs(qa,0,0,rt,rt));
    const double quarterX[3][3]={{1,0,0},{0,0,1},{0,-1,0}};
    assert(QuatFromAxes(quarterX,qa) && QuatIs(qa,rt,0,0,rt));
    const double halfX[3][3]={{1,0,0},{0,-1,0},{0,0,-1}};
    const double halfY[3][3]={{-1,0,0},{0,1,0},{0,0,-1}};
    const double halfZ[3][3]={{-1,0,0},{0,-1,0},{0,0,1}};
    assert(QuatFromAxes(halfX,qa) && QuatIs(qa,1,0,0,0));
    assert(QuatFromAxes(halfY,qa) && QuatIs(qa,0,1,0,0));
    assert(QuatFromAxes(halfZ,qa) && QuatIs(qa,0,0,1,0));
    const double mirrored[3][3]={{-1,0,0},{0,1,0},{0,0,1}};
    const double stretched[3][3]={{2,0,0},{0,1,0},{0,0,1}};
    const double skewed[3][3]={{1,0,0},{1,1,0},{0,0,1}};
    double notNumber[3][3]={{1,0,0},{0,1,0},{0,0,1}};notNumber[1][1]=std::numeric_limits<double>::quiet_NaN();
    assert(!QuatFromAxes(mirrored,qa) && !QuatFromAxes(stretched,qa) && !QuatFromAxes(skewed,qa) && !QuatFromAxes(notNumber,qa));
    const double identity[4]={0,0,0,1},quarter[4]={0,0,rt,rt};
    assert(WorldRotation(axes,identity,w) && QuatIs(w,0,0,0,1));
    assert(WorldRotation(quarterZ,identity,w) && QuatIs(w,0,0,rt,rt));
    assert(WorldRotation(quarterZ,quarter,w) && QuatIs(w,0,0,1,0));
    assert(WorldRotation(axes,quarter,w) && QuatIs(w,0,0,rt,rt));
    const double zero[4]={0,0,0,0};
    assert(!WorldRotation(axes,zero,w) && !WorldRotation(mirrored,identity,w));
    }
    puts("relocation: OK");
}
