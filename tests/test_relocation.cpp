// GPL-3.0-only. Synthetic game-provider fixture; no engine/network access.
#include "game/services/relocation_model.h"
// Assertions perform fixture operations and must remain active in Release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cmath>
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
    // Position decides arrival (0.5 m); a different orientation at the target is still arrival.
    p[0]=0.051;assert(Arrived(r,p,q));p[0]=0.51;assert(!Arrived(r,p,q));p[0]=0;q[0]=1;q[3]=0;assert(Arrived(r,p,q));
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
    {
    // Orientation is measured and reported (1 degree threshold) but never required; qq and -qq are one orientation.
    const double deg=3.14159265358979323846/180.0;
    const auto about=[&](double degrees,double out[4]) { out[0]=0;out[1]=0;out[2]=std::sin(degrees*deg/2);out[3]=std::cos(degrees*deg/2); };
    sc_relocation_request tr{sizeof(tr),1000,5,{1,2,3},{0,0,0,1}};
    const double here[3]={1,2,3};double qq[4];
    about(0.9,qq);assert(CheckArrival(tr,here,qq).rotationNear && Arrived(tr,here,qq));
    about(-0.9,qq);assert(CheckArrival(tr,here,qq).rotationNear && Arrived(tr,here,qq));
    about(0.9,qq);for(double& x:qq)x=-x;assert(CheckArrival(tr,here,qq).rotationNear && Arrived(tr,here,qq));
    about(1.1,qq);assert(!CheckArrival(tr,here,qq).rotationNear && Arrived(tr,here,qq));
    about(-1.1,qq);assert(!CheckArrival(tr,here,qq).rotationNear && Arrived(tr,here,qq));
    about(180,qq);assert(!CheckArrival(tr,here,qq).rotationNear && Arrived(tr,here,qq));
    const double scaled[4]={0,0,0,-5};assert(CheckArrival(tr,here,scaled).rotationNear && Arrived(tr,here,scaled));
    // Position: 0.5 m is in, 0.6 m is out.
    const double edge[3]={1.5,2,3},far[3]={1.6,2,3};about(0,qq);
    assert(Arrived(tr,edge,qq));
    const auto check=CheckArrival(tr,far,qq);assert(check.read && !check.positionNear && check.rotationNear && !check.Near());
    about(2,qq);const auto turned=CheckArrival(tr,here,qq);assert(turned.read && turned.positionNear && !turned.rotationNear && turned.Near());
    const double bad[3]={std::numeric_limits<double>::quiet_NaN(),2,3};
    const auto unread=CheckArrival(tr,bad,qq);assert(!unread.read && !unread.Near());
    }
    {
    // Already at the target when asked: complete at once, nothing applied, no pending slot held.
    Machine local;int ownerA=0;
    sc_relocation_request tr{sizeof(tr),1000,5,{1,2,3},{0,0,0,1}};
    Evidence ev{true,true,true,9,11,7,7,true,true,false,false};
    uint64_t t=0;sc_relocation_status st{};st.size=sizeof(st);
    assert(local.Request(&ownerA,tr,ev,10,&t)==SCO_OK && t);
    assert(local.Status(&ownerA,t,&st)==SCO_OK && st.state==SC_RELOCATION_COMPLETE && st.applied==0 && st.reason==0);
    assert(!local.Pending() && !local.Tick(11,ev));
    uint64_t t2=0;ev.arrived=false;
    assert(local.Request(&ownerA,tr,ev,12,&t2)==SCO_OK && t2>t);
    assert(local.Status(&ownerA,t2,&st)==SCO_OK && st.state==SC_RELOCATION_PENDING);
    assert(local.Tick(13,ev) && local.Pending());
    // Moved but the player is never within 0.5 m: fails by timeout, applied stays 1, and the diagnosis says so.
    local.Applied(true);ev.poseRead=true;ev.positionNear=false;ev.rotationNear=false;ev.metres=0.8;ev.degrees=40;
    Diagnosis why;assert(!local.Tick(12+1000,ev,&why));
    assert(local.Status(&ownerA,t2,&st)==SCO_OK && st.state==SC_RELOCATION_FAILED &&
           st.reason==SC_RELOCATION_REASON_TIMEOUT && st.applied==1);
    assert(why.arrivalUnconfirmed && why.applied && why.streamed && !why.arrived && why.poseRead && !why.positionNear && !why.rotationNear);
    assert(why.firstMetres==0.8 && why.firstDegrees==40 && why.closestMetres==0.8 && why.closestDegrees==40);
    // A timeout where the move never ran is not "arrival not confirmed".
    uint64_t t3=0;assert(local.Request(&ownerA,tr,ev,2000,&t3)==SCO_OK);
    Diagnosis never;assert(!local.Tick(2000+1000,ev,&never));
    assert(never.reason==SC_RELOCATION_REASON_TIMEOUT && !never.arrivalUnconfirmed && !never.applied);
    // A timeout with the target not streamed is not it either.
    uint64_t t4=0;assert(local.Request(&ownerA,tr,ev,4000,&t4)==SCO_OK);local.Applied(true);
    ev.streamed=false;Diagnosis lost;assert(!local.Tick(4000+1000,ev,&lost));
    assert(lost.reason==SC_RELOCATION_REASON_TIMEOUT && !lost.arrivalUnconfirmed);
    // The move is applied once per request, however long the pose stays wrong.
    ev.streamed=true;uint64_t t5=0;assert(local.Request(&ownerA,tr,ev,6000,&t5)==SCO_OK);
    assert(local.Tick(6001,ev));local.Applied(true);
    for(uint64_t at=6002;at<6500;at+=50) assert(!local.Tick(at,ev));
    // A pose that settles on the tick the deadline passes counts as arrived.
    assert(!local.Tick(7000,ev));  // t5 reaches its deadline
    ev.streamed=true;uint64_t t6=0;assert(local.Request(&ownerA,tr,ev,7000,&t6)==SCO_OK);
    assert(local.Tick(7001,ev));local.Applied(true);
    ev.arrived=true;Diagnosis edge;assert(!local.Tick(7000+1000,ev,&edge));
    assert(edge.reason==0 && local.Status(&ownerA,t6,&st)==SCO_OK && st.state==SC_RELOCATION_COMPLETE && st.applied==1);
    ev.arrived=false;
    // The deadline still fails a request that has not arrived.
    uint64_t t7=0;assert(local.Request(&ownerA,tr,ev,8000,&t7)==SCO_OK);
    assert(local.Tick(8001,ev));local.Applied(true);
    ev.metres=0.8;ev.degrees=3.5;Diagnosis late;assert(!local.Tick(8000+1000,ev,&late));
    assert(late.reason==SC_RELOCATION_REASON_TIMEOUT && late.arrivalUnconfirmed && late.metres==0.8 && late.degrees==3.5);
    // The closest approach is kept: first 2.0 m, then 0.7 m (never within 0.5 m), then the player walks away.
    {uint64_t t8=0;ev.metres=-1;ev.degrees=-1;ev.poseRead=false;
     assert(local.Request(&ownerA,tr,ev,9000,&t8)==SCO_OK);
     assert(local.Tick(9001,ev));local.Applied(true);
     ev.poseRead=true;ev.metres=2.0;ev.degrees=90;assert(!local.Tick(9100,ev));
     ev.metres=0.7;ev.degrees=5;assert(!local.Tick(9200,ev));
     ev.metres=3.0;ev.degrees=20;assert(!local.Tick(9300,ev));
     ev.metres=12.351;ev.degrees=95.263;Diagnosis walked;assert(!local.Tick(9000+1000,ev,&walked));
     assert(walked.reason==SC_RELOCATION_REASON_TIMEOUT && walked.arrivalUnconfirmed);
     assert(walked.firstMetres==2.0 && walked.firstDegrees==90 && walked.closestMetres==0.7 && walked.closestDegrees==5);
     assert(walked.metres==12.351 && walked.degrees==95.263);
     char trail[192];FormatTrail(walked,trail,sizeof(trail));
     assert(std::strcmp(trail,"first=2.000 m 90.000 deg closest=0.700 m 5.000 deg last=12.351 m 95.263 deg")==0);
     // Position-only requests print metres only.
     walked.positionOnly=true;FormatTrail(walked,trail,sizeof(trail));
     assert(std::strcmp(trail,"first=2.000 m closest=0.700 m last=12.351 m")==0);
     // Nothing observed after the apply.
     Diagnosis none;FormatTrail(none,trail,sizeof(trail));
     assert(std::strcmp(trail,"first=none closest=none last=not measured")==0);
     char tiny[10];assert(FormatTrail(walked,tiny,sizeof(tiny))>0 && std::strlen(tiny)==9);}
    // Arrives within 0.5 m with a different rotation: complete, the position alone decides.
    {uint64_t t9=0;ev.poseRead=false;ev.metres=-1;ev.degrees=-1;ev.arrived=false;
     assert(local.Request(&ownerA,tr,ev,10000,&t9)==SCO_OK);
     assert(local.Tick(10001,ev));local.Applied(true);
     ev.poseRead=true;ev.metres=0.9;ev.degrees=3;assert(!local.Tick(10100,ev));
     assert(local.Status(&ownerA,t9,&st)==SCO_OK && st.state==SC_RELOCATION_PENDING);
     ev.metres=0.3;ev.degrees=40;assert(!local.Tick(10200,ev));
     assert(local.Status(&ownerA,t9,&st)==SCO_OK && st.state==SC_RELOCATION_COMPLETE && st.applied==1);
     // The player walks away afterwards: the confirmed arrival stays.
     ev.metres=12.351;ev.degrees=95.263;assert(!local.Tick(10300,ev));assert(!local.Tick(10000+1000,ev));
     assert(local.Status(&ownerA,t9,&st)==SCO_OK && st.state==SC_RELOCATION_COMPLETE && st.reason==0);}
    // Arrival is seen on a tick of its own, then the player is gone by the deadline tick: still complete.
    {uint64_t t10=0;ev.metres=-1;ev.degrees=-1;ev.poseRead=false;
     assert(local.Request(&ownerA,tr,ev,11000,&t10)==SCO_OK);
     assert(local.Tick(11001,ev));local.Applied(true);
     ev.poseRead=true;ev.metres=0.5;ev.degrees=10;assert(!local.Tick(11999,ev));
     assert(local.Status(&ownerA,t10,&st)==SCO_OK && st.state==SC_RELOCATION_COMPLETE);
     ev.metres=30;assert(!local.Tick(12000,ev));
     assert(local.Status(&ownerA,t10,&st)==SCO_OK && st.state==SC_RELOCATION_COMPLETE);}
    // A pose read before the move was applied does not count as arrival after it.
    {uint64_t t11=0;ev.poseRead=true;ev.metres=0.1;ev.degrees=0;ev.arrived=false;
     assert(local.Request(&ownerA,tr,ev,13000,&t11)==SCO_OK);
     assert(local.Tick(13001,ev));
     assert(local.Status(&ownerA,t11,&st)==SCO_OK && st.state==SC_RELOCATION_PENDING && !st.applied);
     local.Applied(true);ev.metres=5;ev.degrees=5;Diagnosis pre;assert(!local.Tick(13000+1000,ev,&pre));
     assert(pre.reason==SC_RELOCATION_REASON_TIMEOUT && pre.firstMetres==5 && pre.closestMetres==5);
     ev.poseRead=false;ev.metres=-1;ev.degrees=-1;}
    // CheckArrival measures the offset: 0.6 m and 2 degrees about z.
    {sc_relocation_request mr{sizeof(mr),1000,5,{1,2,3},{0,0,0,1}};
     const double at[3]={1.6,2,3};const double h=std::sin(1.0*3.14159265358979323846/180.0),w=std::cos(1.0*3.14159265358979323846/180.0);
     const double rq[4]={0,0,h,w};const auto o=CheckArrival(mr,at,rq);
     assert(o.read && std::abs(o.metres-0.6)<1e-9 && std::abs(o.degrees-2.0)<1e-6 && !o.positionNear && !o.rotationNear);
     const double none[3]={std::numeric_limits<double>::quiet_NaN(),0,0};
     assert(CheckArrival(mr,none,rq).metres==-1);}
    // PositionDistance and the failure-log formatting of the offset.
    {sc_relocation_position_request dr{sizeof(dr),1000,5,{1,2,3},44};
     const double a[3]={4,6,3};assert(std::abs(PositionDistance(dr,a)-5.0)<1e-12);
     const double bad[3]={0,std::numeric_limits<double>::infinity(),0};assert(PositionDistance(dr,bad)==-1);
     assert(PositionDistance(dr,nullptr)==-1);
     char buf[192];Diagnosis d;
     FormatTrail(d,buf,sizeof(buf));assert(std::strcmp(buf,"first=none closest=none last=not measured")==0);
     d.poseRead=true;d.metres=0.2;d.degrees=3.5;
     FormatTrail(d,buf,sizeof(buf));assert(std::strcmp(buf,"first=none closest=none last=0.200 m 3.500 deg")==0);
     d.positionOnly=true;
     FormatTrail(d,buf,sizeof(buf));assert(std::strcmp(buf,"first=none closest=none last=0.200 m")==0);
     d.positionOnly=false;d.degrees=-1;
     FormatTrail(d,buf,sizeof(buf));assert(std::strcmp(buf,"first=none closest=none last=0.200 m")==0);
     d.metres=-1;
     FormatTrail(d,buf,sizeof(buf));assert(std::strcmp(buf,"first=none closest=none last=not measured")==0);
     d.poseRead=true;d.metres=1234.5678;d.degrees=0.0004;
     char tiny[10];assert(FormatTrail(d,tiny,sizeof(tiny))>0 && std::strlen(tiny)==9);}
    // Position-only: already within 0.5 m completes at once.
    sc_relocation_position_request pr{sizeof(pr),1000,5,{1,2,3},44};
    Evidence pe{true,true,true,9,11,7,7,true,true,false,false,44,77};
    uint64_t pt=0;Machine other;
    assert(other.RequestPosition(&ownerA,pr,pe,10,&pt)==SCO_OK);
    assert(other.Status(&ownerA,pt,&st)==SCO_OK && st.state==SC_RELOCATION_COMPLETE && st.applied==0);
    assert(!other.PendingPosition());
    }
    puts("relocation: OK");
}
