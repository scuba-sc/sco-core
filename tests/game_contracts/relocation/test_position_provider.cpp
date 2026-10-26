// SPDX-License-Identifier: GPL-3.0-only
#include "sco/host.h"
#include "sco/caps.h"
#include "sco/game/services.h"
#include "game/services/relocation.h"
#include "game/admission.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>
namespace S=sco::game::services;
extern "C" sco_result sco_relocation_position_route(const sc_relocation_v1_1*,sco_plugin*,const sc_relocation_position_request*,uint64_t*);
unsigned checks=0;
void Check(bool ok){++checks;if(!ok){std::printf("position provider failed at check%u\n",checks);std::exit(1);}}
enum class Stage { None,Offline,Player,Target,Observe,Clock,Apply };
struct World {
    bool offline=true,player=true,streamed=true,observe=false,apply=true,fault=false,reenter=false,releaseOwner=false,clockFault=false;
    uint64_t playerId=11,playerInstance=21,system=7,targetSystem=7,epoch=40,targetInstance=90,zone=5,now=1000,fullMovedZone=0;
    unsigned reads=0,fullMoves=0,fullPoses=0,positions=0,targets=0,observations=0;
    double pos[3]{1,2,3},rot[4]{1,0,0,0};
    sc_relocation_position_request captured{};
    Stage releaseStage=Stage::None;
    bool retired=false,restart=false;
    unsigned afterRelease=0;
} w;
const sco_api* api=nullptr;sco_plugin* self=nullptr;const sc_relocation_v1_1* table=nullptr;
void Touch(Stage stage){
    if(w.retired){++w.afterRelease;throw 1;}
    if(w.releaseStage==stage){w.releaseStage=Stage::None;w.retired=true;Check(sco::Release(self)==sco::Result::Ok);}
}
sc_relocation_position_request Request(){return {sizeof(sc_relocation_position_request),1000,5,{1,2,3},40};}
sc_relocation_request FullRequest(){return {sizeof(sc_relocation_request),1000,5,{1,2,3},{0,0,0,1}};}
bool Offline(){Touch(Stage::Offline);return w.offline;}
bool Player(S::RelocationPlayer* out){Touch(Stage::Player);++w.reads;if(!w.player)return false;*out={w.playerId,w.playerInstance,w.system};return true;}
void Restart();
bool Target(uint64_t,S::RelocationTarget* out){Touch(Stage::Target);*out={w.targetSystem,w.streamed};Restart();return true;}
bool Move(const sc_relocation_request* r){Touch(Stage::Apply);++w.fullMoves;w.fullMovedZone=r->zone_id;return true;}
bool Pose(uint64_t,double pos[3],double rot[4]){Touch(Stage::Observe);++w.fullPoses;if(!w.observe)return false;std::memcpy(pos,w.pos,sizeof(w.pos));std::memcpy(rot,w.rot,sizeof(w.rot));return true;}
bool PositionTarget(uint64_t zone,S::RelocationPositionTarget* out){
    Touch(Stage::Target);++w.targets;Check(zone==5);*out={w.epoch,w.targetInstance,w.targetSystem,w.streamed};
    if(w.reenter){uint64_t t=99;const auto r=Request();Check(table->request_position(self,&r,&t)==SCO_UNAVAILABLE&&t==99);}
    if(w.releaseOwner){w.releaseOwner=false;Check(sco::Release(self)==sco::Result::Ok);}
    Restart();
    return true;
}
bool ApplyPosition(const sc_relocation_position_request* r,uint64_t instance){
    Touch(Stage::Apply);++w.positions;Check(r&&r->size==sizeof(*r)&&r->zone_id==5&&instance==w.targetInstance);
    w.captured=*r;if(w.fault)throw 1;return w.apply;
}
bool ObservePosition(uint64_t zone,uint64_t instance,double pos[3]){Touch(Stage::Observe);++w.observations;Check(zone==5&&instance==w.targetInstance);if(!w.observe||w.zone!=zone)return false;std::memcpy(pos,w.pos,sizeof(w.pos));return true;}
uint64_t Clock(){Touch(Stage::Clock);if(w.clockFault)throw 1;return w.now;}
S::RelocationBackend Good(){return {true,Offline,Player,Target,Move,Pose,Clock};}
S::RelocationPositionBackend Position(){return {true,PositionTarget,ApplyPosition,ObservePosition};}
void Restart(){
    if(!w.restart)return;
    w.restart=false;S::StopRelocation();auto base=Good();auto position=Position();
    Check(S::StartRelocationV2(&base,&position)==sco::Result::Ok);
    uint64_t ticket=77;auto request=Request();
    Check(table->request_position(self,&request,&ticket)==SCO_UNAVAILABLE&&ticket==77);
}
void Query(){const void* raw=nullptr;Check(api->query_service(SC_RELOCATION_NAME,SC_RELOCATION_VERSION_1_1,&raw)==SCO_OK&&raw);table=static_cast<const sc_relocation_v1_1*>(raw);Check(table->size==sizeof(*table));const void* old=nullptr;Check(api->query_service(SC_RELOCATION_NAME,SC_RELOCATION_VERSION,&old)==SCO_OK&&old==raw);}
void Begin(bool position=true){S::StopRelocation();w=World{};auto base=Good();auto p=Position();Check(S::StartRelocationV2(&base,position?&p:nullptr)==sco::Result::Ok);Query();}
uint64_t Admit(){auto r=Request();uint64_t ticket=0;Check(table->request_position(self,&r,&ticket)==SCO_OK&&ticket);return ticket;}
sc_relocation_status Status(uint64_t ticket){sc_relocation_status s{};s.size=sizeof(s);Check(table->status(self,ticket,&s)==SCO_OK);return s;}
void Tick(){++w.now;S::TickRelocation();}
int main(int argc,char** argv){
    sco::SetGameThread();api=sco::host::BuildApi({"synthetic position relocation"});self=sco::host::NewPlugin("position_test");Check(self!=nullptr);
    Check(sco::AddReleaseHook(S::ReleaseRelocationOwner)==sco::Result::Ok);
    if(argc>1){
        Begin();uint64_t ticket=77;auto request=Request();
        if(!std::strcmp(argv[1],"--clock-request")){
            w.clockFault=true;Check(table->request_position(self,&request,&ticket)==SCO_UNAVAILABLE&&ticket==77&&w.positions==0);
        }else{
            ticket=Admit();w.clockFault=true;Tick();const auto state=Status(ticket);
            Check(state.state==SC_RELOCATION_FAILED&&state.reason==SC_RELOCATION_REASON_FAULT&&state.applied==0&&w.positions==0);
        }
        Check(!api->has(SC_RELOCATION_POSITION_CAPABILITY));S::StopRelocation();
        std::printf("position clock fault: %u checks, 0 failed\n",checks);return 0;
    }
    Begin(false);Check(!api->has(SC_RELOCATION_POSITION_CAPABILITY)&&api->has(SC_RELOCATION_NAME));
    uint64_t unchanged=77;auto r=Request();Check(table->request_position(self,&r,&unchanged)==SCO_UNAVAILABLE&&unchanged==77&&w.positions==0);
    Begin();Check(api->has(SC_RELOCATION_NAME)&&api->has(SC_RELOCATION_POSITION_CAPABILITY));
    auto fullAlias=FullRequest();Check(table->request(self,&fullAlias,&fullAlias.zone_id)==SCO_OK);Tick();
    Check(w.fullMoves==1&&w.fullMovedZone==5);table->cancel(self,fullAlias.zone_id);
    auto positionAlias=Request();Check(table->request_position(self,&positionAlias,&positionAlias.zone_id)==SCO_OK);Tick();
    Check(w.positions==1&&w.captured.zone_id==5);table->cancel(self,positionAlias.zone_id);Begin();
    for(uint32_t bytes=4;bytes<sizeof(*table);++bytes){auto physical=std::make_unique<unsigned char[]>(bytes);std::memcpy(physical.get(),table,bytes);std::memcpy(physical.get(),&bytes,4);Check(sco_relocation_position_route(reinterpret_cast<const sc_relocation_v1_1*>(physical.get()),self,&r,&unchanged)==SCO_UNAVAILABLE&&unchanged==77);}
    auto nullTail=*table;nullTail.request_position=nullptr;Check(sco_relocation_position_route(&nullTail,self,&r,&unchanged)==SCO_UNAVAILABLE&&unchanged==77);
    for(uint32_t bytes=4;bytes<sizeof(r);++bytes){auto physical=std::make_unique<unsigned char[]>(bytes);std::memcpy(physical.get(),&r,bytes);std::memcpy(physical.get(),&bytes,4);Check(table->request_position(self,reinterpret_cast<const sc_relocation_position_request*>(physical.get()),&unchanged)==SCO_BAD_ARG&&unchanged==77);}
    const auto targetReads=w.targets;r.target_epoch=0;Check(table->request_position(self,&r,&unchanged)==SCO_BAD_ARG&&w.targets==targetReads);r=Request();
    w.epoch=41;Check(table->request_position(self,&r,&unchanged)==SCO_UNAVAILABLE&&unchanged==77);w.epoch=40;
    w.targetInstance=0;Check(table->request_position(self,&r,&unchanged)==SCO_UNAVAILABLE&&unchanged==77);w.targetInstance=90;
    uint64_t ticket=Admit();r.pos[0]=999;w.epoch=41;Tick();Check(w.positions==1&&w.captured.pos[0]==1&&w.fullMoves==0&&Status(ticket).state==SC_RELOCATION_PENDING);
    const auto observations=w.observations;Check(Status(ticket).state==SC_RELOCATION_PENDING&&w.observations==observations);
    w.observe=true;w.pos[0]=1.051;Tick();Check(Status(ticket).state==SC_RELOCATION_PENDING);
    w.pos[0]=1;w.zone=6;Tick();Check(Status(ticket).state==SC_RELOCATION_PENDING);w.zone=5;
    w.rot[0]=std::numeric_limits<double>::quiet_NaN();w.epoch=UINT64_MAX;Tick();Check(Status(ticket).state==SC_RELOCATION_COMPLETE&&w.positions==1&&w.fullMoves==0&&w.fullPoses==0);
    Begin();const auto full=FullRequest();Check(table->request(self,&full,&ticket)==SCO_OK);Tick();w.observe=true;Tick();Check(Status(ticket).state==SC_RELOCATION_PENDING&&w.fullMoves==1&&w.positions==0);
    w.rot[0]=0;w.rot[3]=1;Tick();Check(Status(ticket).state==SC_RELOCATION_COMPLETE);
    for(unsigned variant=0;variant<9;++variant){Begin();ticket=Admit();
        uint32_t reason=SC_RELOCATION_REASON_SESSION;
        if(variant==0){w.targetInstance=91;reason=SC_RELOCATION_REASON_TARGET;}
        if(variant==1){w.playerInstance=22;}
        if(variant==2){w.playerId=12;}
        if(variant==3){w.player=false;}
        if(variant==4){w.system=0;reason=SC_RELOCATION_REASON_SYSTEM;}
        if(variant==5){w.targetSystem=8;reason=SC_RELOCATION_REASON_SYSTEM;}
        if(variant==6){w.offline=false;reason=SC_RELOCATION_REASON_SAFETY;}
        if(variant==7){w.targetSystem=0;reason=SC_RELOCATION_REASON_SYSTEM;}
        if(variant==8){S::SetRelocationEnabled(false);reason=SC_RELOCATION_REASON_SAFETY;}
        Tick();const auto s=Status(ticket);Check(s.state==SC_RELOCATION_FAILED&&s.reason==reason&&s.applied==0&&w.positions==0);S::SetRelocationEnabled(true);
    }
    Begin();ticket=Admit();w.streamed=false;Tick();Check(Status(ticket).state==SC_RELOCATION_PENDING&&w.positions==0);w.streamed=true;w.targetInstance=91;Tick();Check(Status(ticket).reason==SC_RELOCATION_REASON_TARGET&&w.positions==0);
    Begin();ticket=Admit();Tick();w.targetInstance=91;Tick();Check(Status(ticket).reason==SC_RELOCATION_REASON_TARGET&&Status(ticket).applied==1&&w.positions==1);
    Begin();ticket=Admit();w.streamed=false;w.now=2000;S::TickRelocation();Check(Status(ticket).reason==SC_RELOCATION_REASON_TIMEOUT&&w.positions==0);
    Begin();ticket=Admit();w.now=999;S::TickRelocation();Check(Status(ticket).reason==SC_RELOCATION_REASON_TIMEOUT);
    Begin();ticket=Admit();Check(table->cancel(self,ticket)==SCO_OK);Tick();Check(w.positions==0&&Status(ticket).state==SC_RELOCATION_CANCELLED);
    ticket=Admit();Tick();Check(table->cancel(self,ticket)==SCO_OK&&Status(ticket).applied==1);Tick();Check(w.positions==1);
    ticket=Admit();Check(table->request(self,&full,&unchanged)==SCO_TOO_MANY&&unchanged==77);S::ReleaseRelocationOwner(self);sc_relocation_status gone{sizeof(gone),0,0,0,0};Check(table->status(self,ticket,&gone)==SCO_NOT_FOUND);Tick();Check(w.positions==1);
    Begin();w.reenter=true;ticket=Admit();w.reenter=false;table->cancel(self,ticket);
    {
        auto* original=self;self=sco::host::NewPlugin("position_released");Check(self!=nullptr);
        Check(sco::Release(self)==sco::Result::Ok);r=Request();
        Check(table->request_position(self,&r,&unchanged)==SCO_BAD_ARG&&unchanged==77);
        Check(table->request(self,&full,&unchanged)==SCO_BAD_ARG&&unchanged==77);
        self=sco::host::NewPlugin("position_reentrant_release");Check(self!=nullptr);w.releaseOwner=true;
        Check(table->request_position(self,&r,&unchanged)==SCO_BAD_ARG&&unchanged==77&&w.positions==0);
        self=sco::host::NewPlugin("position_pending_release");Check(self!=nullptr);ticket=Admit();
        Check(sco::Release(self)==sco::Result::Ok);Tick();Check(w.positions==0);
        self=original;
    }
    {sco_result result=SCO_OK;std::thread worker([&]{auto request=Request();result=table->request_position(self,&request,&unchanged);});worker.join();Check(result==SCO_WRONG_THREAD&&unchanged==77);}
    S::StopRelocation();w=World{};
    sco_relocation_backend_v2 socket{sizeof(socket),0,Player,nullptr,nullptr,nullptr,Clock,PositionTarget,ApplyPosition,ObservePosition};
    for(uint32_t bytes=4;bytes<sizeof(socket);++bytes){auto physical=std::make_unique<unsigned char[]>(bytes);std::memcpy(physical.get(),&socket,bytes);std::memcpy(physical.get(),&bytes,4);Check(sco_game_set_relocation_backend_v2(reinterpret_cast<const sco_relocation_backend_v2*>(physical.get()))==SCO_BAD_ARG);}
    Check(sco_game_set_relocation_backend_v2(nullptr)==SCO_BAD_ARG);
    for(unsigned missing=0;missing<4;++missing){auto bad=socket;if(missing==0)bad.player=nullptr;if(missing==1)bad.position_target=nullptr;if(missing==2)bad.apply_position=nullptr;if(missing==3)bad.observe_position=nullptr;Check(sco_game_set_relocation_backend_v2(&bad)==SCO_BAD_ARG);}
    auto partial=socket;partial.apply=Move;Check(sco_game_set_relocation_backend_v2(&partial)==SCO_BAD_ARG);
    {sco_result result=SCO_OK;std::thread worker([&]{result=sco_game_set_relocation_backend_v2(&socket);});worker.join();Check(result==SCO_WRONG_THREAD);}
    namespace gh=sco::game::host;static gh::Admission admission;gh::detail::SetFreshInspector([]{return admission;});gh::detail::SetFreshAdmissionEnabled(true);
    Check(sco_game_set_relocation_backend_v2(&socket)==SCO_OK);Query();r=Request();const auto readBefore=w.reads;Check(table->request_position(self,&r,&unchanged)==SCO_UNAVAILABLE&&w.reads==readBefore&&w.positions==0);
    admission={gh::Session::Offline,gh::Check::Clear,gh::Check::Clear,gh::Check::Clear};Tick();Check(api->has(SC_RELOCATION_POSITION_CAPABILITY)&&!api->has(SC_RELOCATION_NAME));
    ticket=Admit();Check(table->request(self,&full,&unchanged)==SCO_UNAVAILABLE);Tick();Check(w.positions==1&&Status(ticket).state==SC_RELOCATION_PENDING);w.observe=true;Tick();Check(Status(ticket).state==SC_RELOCATION_COMPLETE);
    Check(sco_game_set_relocation_backend_v2(&socket)==SCO_UNAVAILABLE&&Status(ticket).state==SC_RELOCATION_COMPLETE);
    ticket=Admit();admission.session=gh::Session::Online;Tick();Check(Status(ticket).reason==SC_RELOCATION_REASON_SAFETY&&w.positions==1);
    S::StopRelocation();gh::detail::SetFreshAdmissionEnabled(false);
    auto* original=self;
    for(unsigned mode=0;mode<2;++mode)for(unsigned stage=1;stage<=6;++stage){
        Begin();char id[48];std::snprintf(id,sizeof(id),"release_%u_%u",mode,stage);
        self=sco::host::NewPlugin(id);Check(self!=nullptr);r=Request();
        Check((mode?table->request_position(self,&r,&ticket):table->request(self,&full,&ticket))==SCO_OK);
        if(stage!=6)Tick();
        w.releaseStage=static_cast<Stage>(stage);Tick();
        Check(w.retired&&w.afterRelease==0&&w.positions+w.fullMoves==1);
        std::snprintf(id,sizeof(id),"fresh_%u_%u",mode,stage);self=sco::host::NewPlugin(id);Check(self!=nullptr);
        w.retired=false;
        Check(api->has(SC_RELOCATION_POSITION_CAPABILITY)&&api->has(SC_RELOCATION_NAME));
        Check(table->request_position(self,&r,&ticket)==SCO_OK);Tick();
        Check(Status(ticket).state==SC_RELOCATION_PENDING);table->cancel(self,ticket);
    }
    self=original;
    for(unsigned mode=0;mode<2;++mode)for(unsigned pending=0;pending<2;++pending){
        Begin();r=Request();unchanged=77;
        if(pending)Check((mode?table->request_position(self,&r,&ticket):table->request(self,&full,&ticket))==SCO_OK);
        w.restart=true;
        if(pending)Tick();
        else Check((mode?table->request_position(self,&r,&unchanged):table->request(self,&full,&unchanged))==SCO_UNAVAILABLE&&unchanged==77);
        Check(!w.restart&&w.positions+w.fullMoves==0);
        Check(table->request_position(self,&r,&ticket)==SCO_OK);Tick();
        Check(w.positions==1&&Status(ticket).state==SC_RELOCATION_PENDING);table->cancel(self,ticket);
    }
    Begin();ticket=Admit();w.fault=true;Tick();Check(Status(ticket).reason==SC_RELOCATION_REASON_FAULT&&!api->has(SC_RELOCATION_POSITION_CAPABILITY));
    Begin();r=Request();Check(table->request_position(self,&r,&unchanged)==SCO_UNAVAILABLE&&w.reads==0&&w.positions==0);S::StopRelocation();
    std::printf("position relocation provider: %u checks, 0 failed\n",checks);
}
