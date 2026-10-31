// SPDX-License-Identifier: GPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/game/asop_host/fleet_transaction.h"
#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
using namespace sco::game::asop_host;
struct Fixture {
    enum Mode { Normal, Reenter, StopInside, StopAtPublish, Wait, Unavailable } mode=Normal;
    std::atomic_flag building=ATOMIC_FLAG_INIT;
    std::atomic<bool> admitted{true};
    std::mutex lock, waitLock;
    std::condition_variable cv;
    bool built=false, entered=false, release=false, reentryResult=true;
    int calls=0;
    std::unique_ptr<FleetTable> table=std::make_unique<FleetTable>();
    FleetBuffer buffer{};
    const char* names[2]={"one","two"};
    static bool proceed(void* p) { return static_cast<Fixture*>(p)->admitted.load(); }
    static FleetResolveResult resolve(void* p,const char*,uintptr_t& cls,uint64_t guid[2]) {
        auto& f=*static_cast<Fixture*>(p); ++f.calls;
        if(f.mode==Reenter && f.calls==1) f.reentryResult=f.run();
        if(f.mode==StopInside && f.calls==1) f.stop();
        if(f.mode==Wait && f.calls==1) {
            std::unique_lock<std::mutex> guard(f.waitLock); f.entered=true; f.cv.notify_one();
            f.cv.wait(guard,[&f]{return f.release;});
        }
        if(f.mode==Unavailable && f.calls==2) return FleetResolveResult::Unavailable;
        cls=1; guid[0]=1; guid[1]=0; return FleetResolveResult::Resolved;
    }
    void stop() { std::lock_guard<std::mutex> guard(lock); admitted.store(false); }
    bool run() {
        size_t skipped=0;
        return BuildInitialFleet(building,names,2,resolve,this,std::calloc,proceed,
            [this](const FleetTable& plan,FleetBuffer next) {
                if(mode==StopAtPublish) stop();
                std::lock_guard<std::mutex> guard(lock);
                if(!admitted.load() || built) return false;
                *table=plan; buffer=next; built=true; return true;
            },skipped);
    }
    ~Fixture() { std::free(buffer.data); }
};
int main() {
    {
        Fixture f; f.mode=Fixture::Reenter;
        assert(f.run() && !f.reentryResult && f.calls==2 && f.built && f.buffer.count==2);
        assert(!f.run() && f.buffer.count==2); // already published; abandoned plan freed
    }
    {
        Fixture f; f.mode=Fixture::Unavailable;
        assert(!f.run() && f.calls==2 && !f.built && !f.buffer.data);
        f.mode=Fixture::Normal; f.calls=0;
        assert(f.run() && f.built); // a failed plan does not consume the guard
    }
    {
        Fixture f; f.mode=Fixture::StopInside;
        assert(!f.run() && f.calls==1 && !f.built && !f.buffer.data);
    }
    {
        Fixture f; f.mode=Fixture::StopAtPublish;
        assert(!f.run() && f.calls==2 && !f.built && !f.buffer.data);
    }
    {
        Fixture f; f.mode=Fixture::Wait; bool result=true;
        std::thread worker([&]{result=f.run();});
        { std::unique_lock<std::mutex> guard(f.waitLock); f.cv.wait(guard,[&]{return f.entered;}); }
        assert(!f.run()); // nonblocking concurrent attempt
        f.stop();
        { std::lock_guard<std::mutex> guard(f.waitLock); f.release=true; }
        f.cv.notify_one(); worker.join();
        assert(!result && !f.built && !f.buffer.data && f.calls==1);
    }
}
