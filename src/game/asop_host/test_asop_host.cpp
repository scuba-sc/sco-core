// SPDX-License-Identifier: GPL-3.0-only
#include "state.h"
// Fixture operations and checks must remain active in Release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace sco::game::asop_host;
#include "test_delivery.inc"
#include "test_lift.inc"
#define CHECK(x) assert(x)
#include "test_handoff.inc"
#undef CHECK
int main() {
    delivery_fixture::TestDelivery();
    lift_fixture::TestLift();
    TestHandoff();
    assert(Start(Config{})==Result::Disabled);
    Config c; c.enabled=true;
    assert(Start(c)==Result::AdmissionRefused);
    c.offlinePrepared=true;
    assert(Start(c)==Result::AdmissionRefused);
    c.eacAbsent=true;
    Catalog list;
    const char* names[]={"synthetic_alpha","synthetic_beta"};
    c.catalog=CatalogMode::Ships; c.shipClasses=names; c.shipCount=2;
    assert(list.Configure(c)==Result::Ok && list.count==2);
    Config invalid=c; invalid.size=0; assert(list.Configure(invalid)==Result::InvalidConfig);
    std::vector<std::string> bounded;
    std::vector<const char*> pointers;
    bounded.reserve(kMaxShips); pointers.reserve(kMaxShips);
    for(size_t i=0;i<kMaxShips;++i) bounded.push_back("synthetic_"+std::to_string(i));
    for(const auto& name:bounded) pointers.push_back(name.c_str());
    Config maximum=c; maximum.shipCount=kMaxShips; maximum.shipClasses=pointers.data();
    assert(list.Configure(maximum)==Result::Ok && list.count==kMaxShips);
    assert(list.Configure(c)==Result::Ok && list.count==2);
    c.shipCount=kMaxShips+1;
    assert(list.Configure(c)==Result::InvalidConfig && list.count==2);
    c.shipCount=2; const char* bad[]={"synthetic_alpha","../bad"}; c.shipClasses=bad;
    assert(list.Configure(c)==Result::InvalidConfig);
    const char* duplicates[]={"synthetic_alpha","synthetic_alpha"}; c.shipClasses=duplicates;
    assert(list.Configure(c)==Result::InvalidConfig);
    c.shipClasses=names;
    assert(Start(c)==Result::Unavailable);
    Stop(); Stop();
    assert(Snapshot().stopped && !Snapshot().admitted && Snapshot().ready==0);
    assert(Start(c)==Result::AlreadyStarted);
    std::puts("asop_host: catalog/config/admission, delivery, lift, synchronous lifetime/reentrancy/timeout/shutdown PASS");
}
