// SPDX-License-Identifier: GPL-3.0-only
#include "state.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace sco::game::asop_host;
#include "test_delivery.inc"
int main() {
    delivery_fixture::TestDelivery();
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
    c.shipCount=kMaxShips+1;
    assert(list.Configure(c)==Result::InvalidConfig && list.count==2);
    c.shipCount=2; const char* bad[]={"synthetic_alpha","../bad"}; c.shipClasses=bad;
    assert(list.Configure(c)==Result::InvalidConfig);
    const char* duplicates[]={"synthetic_alpha","synthetic_alpha"}; c.shipClasses=duplicates;
    assert(list.Configure(c)==Result::InvalidConfig);
#ifndef _WIN32
    c.shipClasses=names;
    assert(Start(c)==Result::Unavailable);
#endif
    std::puts("asop_host: bounded catalog/config/admission PASS");
}
