// SPDX-License-Identifier: GPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/game/asop_host/fleet_transaction.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace sco::game::asop_host;
static bool resolve(void*, const char* name, uintptr_t& cls, uint64_t guid[2]) {
    if (!std::strcmp(name,"missing")) return false;
    cls=42; guid[0]=7; guid[1]=9; return true;
}
int main() {
    FleetTable table{}; FleetBuffer buffer{};
    const char* initial[]={"synthetic_alpha","synthetic_beta"};
    assert(RebuildFleet(table,buffer,initial,2,resolve,nullptr,std::calloc));
    auto* retained=buffer.data;
    const auto oldBytes=retained[0x10];
    const char* replace[]={"synthetic_beta","synthetic_gamma"};
    assert(RebuildFleet(table,buffer,replace,2,resolve,nullptr,std::calloc));
    assert(table.count==3 && !std::strcmp(table.slots[0].name,"synthetic_alpha"));
    assert(!table.slots[0].selected && table.slots[1].selected && table.slots[2].selected);
    assert(retained[0x10]==oldBytes && buffer.data!=retained);
    auto idAt=[](const FleetBuffer& b,size_t row) { uint64_t id=0; std::memcpy(&id,b.data+row*kFleetEntitlementSize+0x10,8); return id; };
    assert(idAt(buffer,0)==(kFleetUrnMarker|1) && idAt(buffer,1)==(kFleetUrnMarker|2));
    const auto before=table; const auto published=buffer;
    const char* missing[]={"missing"};
    assert(!RebuildFleet(table,buffer,missing,1,resolve,nullptr,std::calloc));
    assert(!std::memcmp(&before,&table,sizeof(table)) && buffer.data==published.data && buffer.count==published.count);
    auto failAlloc=+[](size_t,size_t)->void* { return nullptr; };
    assert(!RebuildFleet(table,buffer,initial,2,resolve,nullptr,failAlloc));
    assert(!std::memcmp(&before,&table,sizeof(table)) && buffer.data==published.data);
    assert(RebuildFleet(table,buffer,initial,2,resolve,nullptr,std::calloc));
    assert(table.count==3 && table.slots[0].selected && !table.slots[2].selected);
    std::free(retained); std::free(published.data); std::free(buffer.data);
    table.count=kFleetCapacity;
    const char* overflow[]={"synthetic_new"};
    const auto full=table; const auto oldBuffer=buffer;
    assert(!RebuildFleet(table,buffer,overflow,1,resolve,nullptr,std::calloc));
    assert(!std::memcmp(&full,&table,sizeof(table)) && buffer.data==oldBuffer.data);
    const char* duplicates[]={"synthetic_alpha","SYNTHETIC_ALPHA"};
    assert(!RebuildFleet(table,buffer,duplicates,2,resolve,nullptr,std::calloc));
    assert(!std::memcmp(&full,&table,sizeof(table)));
    std::puts("fleet transaction: stable slots, retained buffers, resolver/allocation rollback PASS");
}
