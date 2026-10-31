// SPDX-License-Identifier: GPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/game/asop_host/state.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
using namespace sco::game::asop_host;
static bool gameThread=true;
static int rebuildCalls=0, gameThreadCalls=0, closeFleetCalls=0, closeHangarCalls=0;
namespace sco { bool OnGameThread() { ++gameThreadCalls; return gameThread; } }
namespace sco::game::asop_host {
bool InstallCatalog() { return true; }
void InstallFleet() {} void InstallHangars() {} void SetFleetCaps() {} void SetHangarCaps() {}
void LogFleet() {} void LogHangars() {}
void CloseFleet() { ++closeFleetCalls; } void CloseHangars() { ++closeHangarCalls; }
void TickFleet(uint32_t) {} void TickHangars(uint32_t) {}
uint32_t FleetReady() { return 0; } uint32_t HangarReady() { return 0; }
bool RebuildFleetShips(const Catalog&) { ++rebuildCalls; return true; }
}
static void AssertSameStatus(const Status& before) {
    const auto after=Snapshot();
    assert(after.configured==before.configured && after.admitted==before.admitted);
    assert(after.installed==before.installed && after.stopped==before.stopped);
    assert(after.catalogCount==before.catalogCount && after.ready==before.ready);
}
int main(int argc, char**) {
    Config config; config.enabled=config.offlinePrepared=config.eacAbsent=true;
    config.catalog=CatalogMode::Ships;
    const char* first[]={"synthetic_alpha"}; config.shipClasses=first; config.shipCount=1;
    const auto initial=Snapshot();
    assert(Reconfigure(config)==Result::Unavailable);
    AssertSameStatus(initial);
    assert(Start(config)==Result::Ok);
    const auto started=Snapshot();
    const auto unchanged=[&] {
        assert(rebuildCalls==0 && gameThreadCalls==0);
        assert(closeFleetCalls==0 && closeHangarCalls==0);
        assert(catalog.count==1 && std::strcmp(catalog.names[0].data(),"synthetic_alpha")==0);
        assert(settings.enabled && settings.offlinePrepared && settings.eacAbsent);
        assert(!settings.diagnostics && settings.catalog==CatalogMode::Ships);
        assert(settings.shipClasses==nullptr && settings.shipCount==0 && admitted.load());
        AssertSameStatus(started);
    };
    const char* second[]={"synthetic_beta","synthetic_gamma"};
    config.shipClasses=second; config.shipCount=2; config.diagnostics=true;
    // Even a valid request must refuse before entering the engine rebuild seam.
    assert(Reconfigure(config)==Result::Unavailable); unchanged();
    config.shipClasses=nullptr; config.shipCount=0;
    assert(Reconfigure(config)==Result::Unavailable); unchanged();
    gameThread=false;
    assert(Reconfigure(config)==Result::Unavailable); unchanged();
    gameThread=true; config.eacAbsent=false;
    assert(Reconfigure(config)==Result::Unavailable); unchanged();
    config.eacAbsent=true; config.catalog=CatalogMode::Game;
    assert(Reconfigure(config)==Result::Unavailable); unchanged();
    config.catalog=CatalogMode::Ships;
    const char* duplicates[]={"synthetic_alpha","SYNTHETIC_ALPHA"};
    config.shipClasses=duplicates; config.shipCount=2;
    assert(Reconfigure(config)==Result::Unavailable); unchanged();
    config.shipClasses=second;
    // These modes exercise terminal Stop through synthetic close seams only.
    // There is no rebuild callback or engine rebuild/Stop overlap to certify.
    if(argc>1) {
        std::thread stopper([] { Stop(); });
        stopper.join();
    } else {
        Stop();
    }
    assert(closeFleetCalls==1 && closeHangarCalls==1);
    const auto stopped=Snapshot();
    assert(stopped.configured && stopped.installed && stopped.stopped && !stopped.admitted);
    assert(!admitted.load());
    assert(Reconfigure(config)==Result::Unavailable);
    AssertSameStatus(stopped);
    assert(rebuildCalls==0 && gameThreadCalls==0);
    assert(catalog.count==1 && stopped.catalogCount==1);
    assert(std::strcmp(catalog.names[0].data(),"synthetic_alpha")==0);
    assert(Start(config)==Result::AlreadyStarted);
    Stop(); assert(closeFleetCalls==2 && closeHangarCalls==2);
    std::puts("asop reconfigure: unavailable, zero engine calls, prior state, terminal Stop seams PASS");
}
