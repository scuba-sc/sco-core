// SPDX-License-Identifier: GPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/game/asop_host/state.h"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace sco::game::asop_host;
static int installCalls=0, engineCalls=0, closeCalls=0, threadQueries=0;
namespace sco { bool OnGameThread() { ++threadQueries; return true; } }
namespace sco::game::asop_host {
bool InstallCatalog() { ++installCalls; return true; }
void InstallFleet() { ++installCalls; } void InstallHangars() { ++installCalls; }
void SetFleetCaps() { ++engineCalls; } void SetHangarCaps() { ++engineCalls; }
void LogFleet() { ++engineCalls; } void LogHangars() { ++engineCalls; }
void CloseFleet() { ++closeCalls; } void CloseHangars() { ++closeCalls; }
void TickFleet(uint32_t) { ++engineCalls; } void TickHangars(uint32_t) { ++engineCalls; }
uint32_t FleetReady() { return 0; } uint32_t HangarReady() { return 0; }
bool RebuildFleetShips(const Catalog&) { ++engineCalls; return true; }
void DisableFleetAdmission() { admitted.store(false); }
void PumpInitialFleet() { ++engineCalls; }
}
static void AssertNoAdmissionOrMutation() {
    const auto status=Snapshot();
    assert(!status.configured && !status.admitted && !status.installed && !status.stopped);
    assert(status.catalogCount==0 && status.ready==0 && !admitted.load());
    assert(catalog.count==0 && !settings.enabled);
    assert(installCalls==0 && engineCalls==0 && closeCalls==0 && threadQueries==0);
}
int main(int argc, char**) {
    Config c;
    const char* names[]={"synthetic_alpha"}; c.shipClasses=names; c.shipCount=1;
    c.catalog=CatalogMode::Ships;
    assert(Start(c)==Result::Disabled); AssertNoAdmissionOrMutation();
    c.enabled=true;
    assert(Start(c)==Result::AdmissionRefused); AssertNoAdmissionOrMutation();
    c.offlinePrepared=true;
    assert(Start(c)==Result::AdmissionRefused); AssertNoAdmissionOrMutation();
    c.eacAbsent=true;
    Config invalid=c; invalid.size=0;
    assert(Start(invalid)==Result::InvalidConfig); AssertNoAdmissionOrMutation();
    const char* duplicate[]={"synthetic_alpha","SYNTHETIC_ALPHA"};
    invalid=c; invalid.shipClasses=duplicate; invalid.shipCount=2;
    assert(Start(invalid)==Result::InvalidConfig); AssertNoAdmissionOrMutation();
    assert(Start(c)==Result::Unavailable); AssertNoAdmissionOrMutation();
    assert(Reconfigure(c)==Result::Unavailable); AssertNoAdmissionOrMutation();
    assert(Start(c)==Result::Unavailable); AssertNoAdmissionOrMutation();
    Pump(1); Pump(1000); AssertNoAdmissionOrMutation();
    // Stop without admission is terminal and must not enter close/mutation seams.
    if(argc>1) { std::thread stopper([] { Stop(); }); stopper.join(); }
    else Stop();
    const auto stopped=Snapshot();
    assert(stopped.stopped && !stopped.configured && !stopped.admitted && !stopped.installed && stopped.ready==0);
    assert(!admitted.load() && installCalls==0 && engineCalls==0 && closeCalls==0 && threadQueries==0);
    assert(Start(c)==Result::AlreadyStarted && Reconfigure(c)==Result::Unavailable);
    Pump(2000); Stop();
    assert(installCalls==0 && engineCalls==0 && closeCalls==0 && threadQueries==0);
    std::puts("asop startup: refused before admission and native mutation; terminal Stop PASS");
}
