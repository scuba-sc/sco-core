// SPDX-License-Identifier: GPL-3.0-only
#include "game/services/relocation_model.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace sco::game::services::relocation;
unsigned checks=0;
void Check(bool ok){++checks;if(!ok){std::printf("position relocation failed at check%u\n",checks);std::exit(1);}}
int main(){
    Machine machine;const void* owner=reinterpret_cast<void*>(1);const void* other=reinterpret_cast<void*>(2);
    sc_relocation_position_request r{sizeof(r),1000,5,{1,2,3},40};
    Evidence e{true,true,true,7,11,9,9,true,false,false,false};e.targetEpoch=40;e.targetInstance=90;
    uint64_t ticket=77;sc_relocation_status s{sizeof(s),99,99,99,99};
    for(int variant=0;variant<8;++variant){auto bad=r;
        if(variant==0)bad.size=sizeof(bad)-1;
        if(variant==1)bad.zone_id=0;
        if(variant==2)bad.target_epoch=0;
        if(variant==3)bad.timeout_ms=0;
        if(variant==4)bad.timeout_ms=60001;
        if(variant==5)bad.pos[0]=1e12+1;
        if(variant==6)bad.pos[0]=std::numeric_limits<double>::infinity();
        if(variant==7)bad.pos[0]=std::numeric_limits<double>::quiet_NaN();
        Check(machine.RequestPosition(owner,bad,e,100,&ticket)==SCO_BAD_ARG&&ticket==77);
    }
    for(int variant=0;variant<11;++variant){auto bad=e;
        if(variant==0)bad.offline=false;
        if(variant==1)bad.transport=false;
        if(variant==2)bad.player=false;
        if(variant==3)bad.session=0;
        if(variant==4)bad.currentSystem=0;
        if(variant==5)bad.targetSystem=10;
        if(variant==6)bad.streamed=false;
        if(variant==7)bad.fault=true;
        if(variant==8)bad.targetEpoch=41;
        if(variant==9)bad.targetInstance=0;
        if(variant==10)bad.targetEpoch=0;
        Check(machine.RequestPosition(owner,r,bad,100,&ticket)==SCO_UNAVAILABLE&&ticket==77);
    }
    Check(machine.RequestPosition(owner,r,e,100,&ticket)==SCO_OK);
    Check(!machine.Pending()&&machine.PendingPosition()&&machine.PendingTargetInstance()==90);
    sc_relocation_request full{sizeof(full),1000,5,{1,2,3},{0,0,0,1}};uint64_t unchanged=88;
    Check(machine.Request(other,full,e,100,&unchanged)==SCO_TOO_MANY&&unchanged==88);
    Check(machine.RequestPosition(other,r,e,100,&unchanged)==SCO_TOO_MANY&&unchanged==88);
    Check(machine.Cancel(other,ticket)==SCO_NOT_FOUND&&machine.Status(other,ticket,&s)==SCO_NOT_FOUND&&s.state==99);
    e.arrived=true;Check(machine.Tick(101,e));machine.Applied(true);
    e.targetEpoch=999;Check(!machine.Tick(102,e));Check(machine.Status(owner,ticket,&s)==SCO_OK&&s.state==SC_RELOCATION_COMPLETE);
    e.targetEpoch=r.target_epoch;e.arrived=false;
    Check(machine.Request(owner,full,e,200,&ticket)==SCO_OK);
    Check(machine.RequestPosition(other,r,e,200,&unchanged)==SCO_TOO_MANY);
    Check(machine.Tick(201,e));machine.Applied(true);
    const double pos[]{1,2,3},wrongRotation[]{1,0,0,0};
    Check(ArrivedPosition(r,pos)&&!Arrived(full,pos,wrongRotation));
    Check(machine.Status(owner,ticket,&s)==SCO_OK&&s.state==SC_RELOCATION_PENDING);machine.Cancel(owner,ticket);
    for(int variant=0;variant<8;++variant){e.targetEpoch=40;e.targetInstance=90;Check(machine.RequestPosition(owner,r,e,300,&ticket)==SCO_OK);auto changed=e;uint32_t reason=0;
        if(variant==0){changed.targetInstance=91;reason=SC_RELOCATION_REASON_TARGET;}
        if(variant==1){changed.targetInstance=0;reason=SC_RELOCATION_REASON_TARGET;}
        if(variant==2){changed.session=8;reason=SC_RELOCATION_REASON_SESSION;}
        if(variant==3){changed.playerId=12;reason=SC_RELOCATION_REASON_SESSION;}
        if(variant==4){changed.targetSystem=10;reason=SC_RELOCATION_REASON_SYSTEM;}
        if(variant==5){changed.offline=false;reason=SC_RELOCATION_REASON_SAFETY;}
        if(variant==6){changed.transport=false;reason=SC_RELOCATION_REASON_TRANSPORT;}
        if(variant==7){changed.fault=true;reason=SC_RELOCATION_REASON_FAULT;}
        Check(!machine.Tick(301,changed));Check(machine.Status(owner,ticket,&s)==SCO_OK&&s.state==SC_RELOCATION_FAILED&&s.reason==reason&&s.applied==0);
    }
    Check(machine.RequestPosition(owner,r,e,400,&ticket)==SCO_OK);e.streamed=false;e.targetInstance=0;
    Check(!machine.Tick(401,e));Check(machine.Status(owner,ticket,&s)==SCO_OK&&s.state==SC_RELOCATION_PENDING);
    e.streamed=true;e.targetInstance=91;Check(!machine.Tick(402,e));Check(machine.Status(owner,ticket,&s)==SCO_OK&&s.reason==SC_RELOCATION_REASON_TARGET);
    e.targetInstance=90;Check(machine.RequestPosition(owner,r,e,500,&ticket)==SCO_OK);e.streamed=false;
    Check(!machine.Tick(1500,e));Check(machine.Status(owner,ticket,&s)==SCO_OK&&s.reason==SC_RELOCATION_REASON_TIMEOUT);e.streamed=true;
    Check(machine.RequestPosition(owner,r,e,1600,&ticket)==SCO_OK);Check(machine.Cancel(owner,ticket)==SCO_OK&&!machine.Tick(1601,e));
    Check(machine.RequestPosition(owner,r,e,1700,&ticket)==SCO_OK);Check(machine.Tick(1701,e));machine.Applied(true);Check(machine.Cancel(owner,ticket)==SCO_OK);
    Check(machine.Status(owner,ticket,&s)==SCO_OK&&s.applied==1&&s.state==SC_RELOCATION_CANCELLED);
    const uint64_t evicted=ticket;
    for(int i=0;i<33;++i){Check(machine.RequestPosition(owner,r,e,1800,&ticket)==SCO_OK);Check(machine.Cancel(owner,ticket)==SCO_OK);}
    Check(machine.Status(owner,evicted,&s)==SCO_NOT_FOUND);machine.Release(owner);Check(machine.Status(owner,ticket,&s)==SCO_NOT_FOUND);
    const uint64_t previous=ticket;machine.Stop();Check(machine.RequestPosition(owner,r,e,1900,&ticket)==SCO_OK&&ticket>previous);machine.Stop();
    double far[]{1.051,2,3},nan[]{1,2,std::numeric_limits<double>::quiet_NaN()};Check(!ArrivedPosition(r,far)&&!ArrivedPosition(r,nan)&&!ArrivedPosition(r,nullptr));
    auto fullAlias=full;
    Check(machine.Request(owner,fullAlias,e,2000,&fullAlias.zone_id)==SCO_OK);
    Check(machine.Pending()&&machine.Pending()->zone_id==5);machine.Stop();
    auto positionAlias=r;
    Check(machine.RequestPosition(owner,positionAlias,e,2100,&positionAlias.zone_id)==SCO_OK);
    Check(machine.PendingPosition()&&machine.PendingPosition()->zone_id==5);machine.Stop();
    std::printf("position relocation model: %u checks, 0 failed\n",checks);
}
