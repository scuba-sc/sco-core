// SPDX-License-Identifier: GPL-3.0-only
// Synthetic collector/policy logic only; NOT Windows privilege acceptance.
#include "ipc/diagnostics_ipc.h"
#include "sco/app.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
using namespace sco::diagnostics;
namespace di=sco::diagnostics::ipc;
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": "<<#x<<'\n';std::abort();}}while(0)
struct Collector final:audit::Collector {
    audit::Status status=audit::Status::Accepted; unsigned events=0;
    std::shared_ptr<std::atomic<OfflineAdmission>> admission; bool revoke=false;
    audit::Receipt Verify(uint64_t) override {return {audit::Status::Verified,di::ClockNow(),true,true};}
    audit::Receipt Append(const di::AuditEvent& e,const audit::Coordinates&,uint64_t deadline) override {
        ++events;if(revoke&&e.opcode==0x100)admission->store(OfflineAdmission::Denied);
        return {status,status==audit::Status::Deadline?deadline:di::ClockNow(),true,true};
    }
};
int main(int argc,char** argv){
    CHECK(argc==3);sco::SetGameThread();sco::app::Platform defaults;
    CHECK(!defaults.runtimeInspection.enabled&&!defaults.runtimeInspection.disclosureConsent);
    SnapshotStore snapshots;Capture capture;di::Host host(snapshots,capture);di::Options o;
    auto collector=std::make_shared<Collector>();o.policy.collector=collector;
    o.policy.admission=collector->admission=std::make_shared<std::atomic<OfflineAdmission>>(OfflineAdmission::Unknown);
    CHECK(host.Start(o,di::ClockNow())==di::Result::Disabled);
    o.enabled=o.policy.enabled=o.policy.disclosureConsent=true;
    for(auto posture:{OfflineAdmission::Unknown,OfflineAdmission::Online,OfflineAdmission::Denied}){
        o.policy.admission->store(posture);CHECK(host.Start(o,di::ClockNow())==di::Result::Disabled);CHECK(!host.Started());
    }
    const auto dir=std::filesystem::path(argv[1])/"runtime-policy";
    std::filesystem::create_directories(dir);std::filesystem::permissions(dir,std::filesystem::perms::owner_all);
    o.credentialPath=(dir/"session.json").string();o.excludedRoots={argv[2]};o.hostBuild="fixture-host-1";
    o.policy.admission->store(OfflineAdmission::Authorized);
    for(auto status:{audit::Status::Unavailable,audit::Status::Full,audit::Status::Deadline}){
        collector->status=status;CHECK(host.Start(o,di::ClockNow())==di::Result::Audit);CHECK(!host.Started());
    }
    collector->status=audit::Status::Accepted;CHECK(host.Start(o,di::ClockNow())==di::Result::Ok);
    di::Peer peer;CHECK(peer.Begin(o.credentialPath,di::ClockNow())==di::Result::Ok);
    CHECK(host.Poll(di::ClockNow())==di::Result::Ok);CHECK(peer.Challenge(di::ClockNow())==di::Result::Ok);
    CHECK(host.Poll(di::ClockNow())==di::Result::Ok);CHECK(peer.Ready(di::ClockNow())==di::Result::Ok);
    collector->revoke=true;CHECK(peer.Send(0x100,"{}",di::ClockNow())==di::Result::Ok);
    CHECK(host.Poll(di::ClockNow())==di::Result::Disabled);CHECK(!host.Started());
    std::string output;CHECK(peer.Receive(di::ClockNow(),output)!=di::Result::Ok&&output.empty());
    CHECK(!std::filesystem::exists(o.credentialPath));peer.Close();host.Stop();std::filesystem::remove(dir);
    std::cout<<"policy defaults, posture, audit failures and pending-read revocation passed (synthetic)\n";
}
