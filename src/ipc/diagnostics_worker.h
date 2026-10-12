// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "diagnostics_ipc.h"
#include <atomic>
#include <thread>
namespace sco::diagnostics::ipc {
// Collector work never executes on the game thread. Stop revokes before join.
class Worker {
    SnapshotStore& snapshots_; Capture& capture_;
    InspectionOptions options_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
public:
    Worker(SnapshotStore& snapshots,Capture& capture):snapshots_(snapshots),capture_(capture){}
    ~Worker(){Stop();}
    void Start(const InspectionOptions& options) {
        Stop(); options_=options;
        if(!Authorized(options_))return;
        stopping_=false;
        thread_=std::thread([this]{
            Options o;o.enabled=true;o.policy=options_;o.credentialPath=options_.credentialPath;
            o.excludedRoots=options_.excludedRoots;o.hostBuild=options_.hostBuild;
            Host host(snapshots_,capture_);
            if(host.Start(o,ClockNow())!=Result::Ok){capture_.Stop();return;}
            while(!stopping_.load(std::memory_order_acquire)&&Authorized(options_)){
                if(host.Poll(ClockNow())!=Result::Ok)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            host.Stop();capture_.Stop();
        });
    }
    void Stop() {
        stopping_.store(true,std::memory_order_release);
        if(options_.admission)options_.admission->store(OfflineAdmission::Denied,std::memory_order_release);
        capture_.Stop();
        if(thread_.joinable())thread_.join(); // <=collector deadline; stop latency NOT VERIFIED
    }
};
}
