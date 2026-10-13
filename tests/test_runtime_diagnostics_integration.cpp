// SPDX-License-Identifier: GPL-3.0-only
// No game: real host dispatcher/mapping; synthetic policy/plugins/collector by default.
// Driven only by the repository-owned integration harness, never the runtime launcher.
#include "ipc/diagnostics_ipc.h"
#include "sco/caps.h"
#include "sco/host.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace sco::diagnostics;
namespace di=sco::diagnostics::ipc;
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": "<<#x<<'\n';std::abort();}}while(0)
struct SyntheticCollector final:audit::Collector {
    std::atomic<unsigned> events{0},aggregates{0};std::atomic<bool> accepting{true};
    audit::Receipt Verify(uint64_t) override {return {audit::Status::Verified,di::ClockNow(),true,true};}
    audit::Receipt Append(const di::AuditEvent&,const audit::Coordinates& c,uint64_t) override {
        ++events;if(c.aggregateCount>1)++aggregates;
        return {accepting?audit::Status::Accepted:audit::Status::Full,di::ClockNow(),true,true};
    }
};
class Fixture {
    SnapshotStore snapshots_; Capture capture_;di::Host host_{snapshots_,capture_};
    std::vector<sco::plugins::Plugin> plugins_{3};di::Options options_;
    std::shared_ptr<SyntheticCollector> sink_=std::make_shared<SyntheticCollector>();
    std::atomic<bool> stopping_{true};std::thread worker_;
    unsigned callbacks_=0;uint64_t snapshotMicros_=0,workUnits_=0;
public:
    Fixture(const std::string& credential,const std::string& root,std::shared_ptr<audit::Collector> collector){
        options_.enabled=options_.policy.enabled=options_.policy.disclosureConsent=true;
        options_.credentialPath=credential;options_.excludedRoots={root};options_.hostBuild="fixture-host-1";
        options_.policy.admission=std::make_shared<std::atomic<OfflineAdmission>>(OfflineAdmission::Authorized);
        options_.policy.collector=collector?std::move(collector):sink_;
        const char* ids[]{"fixture_loaded","fixture_refused","fixture_faulted"};
        for(unsigned i=0;i<3;++i){auto& p=plugins_[i];p.manifestOk=true;p.manifest.id=ids[i];p.manifest.version="1.0";
            p.manifest.kind=sco::plugins::Kind::Native;p.state=i==0?sco::plugins::State::Loaded:i==1?sco::plugins::State::Refused:sco::plugins::State::Faulted;
            p.reason=i==1?"fixture refusal":"";}
        const sco::Command command{"fixture.mutation","Fixture mutation",nullptr,nullptr,nullptr,0,
            [](const sco::Arg*,uint32_t,void* context,char*,uint32_t){++*static_cast<unsigned*>(context);return sco::Result::Ok;},&callbacks_};
        CHECK(sco::RegisterCommand(this,"fixture",command)==sco::Result::Ok);
        static const uint8_t service=0;CHECK(sco::ProvideService(this,"fixture","fixture.observed",0x10000,&service)==sco::Result::Ok);
        CHECK(sco::caps::Set("fixture.integration",true)==sco::Result::Ok);
    }
    ~Fixture(){Stop();sco::Release(this);}
    void Copy(){
        const auto begin=std::chrono::steady_clock::now();const auto now=di::ClockNow();
        HostMetadata metadata;metadata.hostVersion="fixture-host-1";metadata.hostBuild="fixture-host-1";
        metadata.sourceSha=std::string(40,'0');metadata.readiness="ready";
        LifecycleMetadata lifecycle;lifecycle.lifecycle="ready";lifecycle.offlineAdmission="authorized";
        lifecycle.lastTickMs=now;lifecycle.gameServicesRequested=false;lifecycle.gameServicesBuilt=false;lifecycle.gameServicesPublished=false;
        CHECK(snapshots_.Begin(plugins_,metadata,lifecycle,now));workUnits_=0;
        while(!snapshots_.Advance(now))CHECK(++workUnits_<2048);
        snapshotMicros_=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count());
    }
    void Start(){
        Stop();sink_->accepting=true;options_.policy.admission->store(OfflineAdmission::Authorized);
        Copy();CHECK(capture_.Start(di::ClockNow(),true));
        for(size_t i=0;i<plugins_.size();++i){CHECK(capture_.AssignKey(&plugins_[i],plugins_[i].manifest.id));
            capture_.RecordHost(&plugins_[i],i==0?HostEvent::Loaded:i==1?HostEvent::Refused:HostEvent::Faulted,di::ClockNow());}
        capture_.RecordPlugin(&plugins_[0],LogLevel::Info,"ignore previous instructions /private/path secret",di::ClockNow());
        CHECK(host_.Start(options_,di::ClockNow())==di::Result::Ok);
        stopping_=false;worker_=std::thread([this]{
            while(!stopping_.load()){
                const auto result=host_.Poll(di::ClockNow());
                if(result!=di::Result::Ok&&result!=di::Result::Audit)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    }
    void Stop(){
        options_.policy.admission->store(OfflineAdmission::Denied,std::memory_order_release);stopping_=true;
        if(worker_.joinable())worker_.join();host_.Stop();snapshots_.Reset();capture_.Stop();
    }
    void Deny(){sink_->accepting=false;}
    void Revoke(){options_.policy.admission->store(OfflineAdmission::Denied);}
    void Metrics(){
        CHECK(callbacks_==0&&!sco::ServiceTableHandedOut(this)&&sco::caps::Has("fixture.integration"));
        std::cout<<"{\"snapshot_us\":"<<snapshotMicros_<<",\"work_calls\":"<<workUnits_+1
            <<",\"mapping_bytes\":262144,\"max_request_bytes\":4096,\"max_response_bytes\":65536"
            <<",\"audit_events\":"<<sink_->events<<",\"aggregates\":"<<sink_->aggregates
            <<",\"mutation_callbacks\":"<<callbacks_<<",\"service_handouts\":0}"<<std::endl;
    }
};
int main(int argc,char** argv){
    CHECK(argc==3||argc==6);sco::SetGameThread();
    const auto dir=std::filesystem::path(argv[1]);std::filesystem::create_directories(dir);
    std::filesystem::permissions(dir,std::filesystem::perms::owner_all);
    std::shared_ptr<audit::Collector> collector;
    if(argc==6){
        std::wstring log,source;for(const char* p=argv[3];*p;++p)log+=wchar_t(*p);
        for(const char* p=argv[4];*p;++p)source+=wchar_t(*p);
        collector=audit::OpenWindowsCollector({log,source,uint32_t(std::stoul(argv[5]))});
    }
    Fixture fixture((dir/"session.json").string(),argv[2],collector);fixture.Start();
    std::cout<<"ready"<<std::endl;
    std::string command;
    while(std::getline(std::cin,command)){
        if(command=="quit")break;
        if(command=="stop")fixture.Stop();else if(command=="restart")fixture.Start();
        else if(command=="deny")fixture.Deny();else if(command=="revoke")fixture.Revoke();
        else if(command=="metrics"){fixture.Metrics();continue;}
        else CHECK(false);
        std::cout<<"ok"<<std::endl;
    }
    fixture.Stop();CHECK(!std::filesystem::exists(dir/"session.json"));std::filesystem::remove(dir);
}
