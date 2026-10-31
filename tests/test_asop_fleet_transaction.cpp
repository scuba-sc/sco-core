// SPDX-License-Identifier: GPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/game/asop_host/fleet_transaction.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
using namespace sco::game::asop_host;
static FleetResolveResult resolve(void*, const char* name, uintptr_t& cls, uint64_t guid[2]) {
    if (!std::strcmp(name,"missing") || !std::strcmp(name,"MISSING")) return FleetResolveResult::Missing;
    if (!std::strcmp(name,"null_class")) { cls=0; guid[0]=7; return FleetResolveResult::Resolved; }
    if (!std::strcmp(name,"zero_guid")) { cls=42; guid[0]=guid[1]=0; return FleetResolveResult::Resolved; }
    cls=42; guid[0]=7; guid[1]=9; return FleetResolveResult::Resolved;
}
static uint64_t idAt(const FleetBuffer& b,size_t row) {
    uint64_t id=0; std::memcpy(&id,b.data+row*kFleetEntitlementSize+0x10,8); return id;
}
int main() {
    // FleetTable is large; keep snapshots off the default Windows thread stack.
    auto table=std::make_unique<FleetTable>(); FleetBuffer buffer{};
    const char* initial[]={"synthetic_alpha","synthetic_beta"};
    size_t skipped=99;
    assert(RebuildFleet(*table,buffer,initial,2,resolve,nullptr,std::calloc,&skipped));
    assert(skipped==0 && HasFleetEntitlements(buffer));
    auto* retained=buffer.data;
    const auto oldBytes=retained[0x10];
    const char* replace[]={"synthetic_beta","synthetic_gamma"};
    assert(RebuildFleet(*table,buffer,replace,2,resolve,nullptr,std::calloc));
    assert(table->count==3 && !std::strcmp(table->slots[0].name,"synthetic_alpha"));
    assert(!table->slots[0].selected && table->slots[1].selected && table->slots[2].selected);
    assert(retained[0x10]==oldBytes && buffer.data!=retained);
    assert(idAt(buffer,0)==(kFleetUrnMarker|1) && idAt(buffer,1)==(kFleetUrnMarker|2));
    auto before=std::make_unique<FleetTable>(*table); const auto published=buffer;
    const char* missing[]={"missing"};
    assert(!RebuildFleet(*table,buffer,missing,1,resolve,nullptr,std::calloc));
    assert(!std::memcmp(before.get(),table.get(),sizeof(*table)) && buffer.data==published.data && buffer.count==published.count);
    // A caller cannot opt back into initial skip mode after publication.
    skipped=99;
    assert(!RebuildFleet(*table,buffer,missing,1,resolve,nullptr,std::calloc,&skipped));
    assert(skipped==99 && !std::memcmp(before.get(),table.get(),sizeof(*table)) && buffer.data==published.data);
    auto failAlloc=+[](size_t,size_t)->void* { return nullptr; };
    assert(!RebuildFleet(*table,buffer,initial,2,resolve,nullptr,failAlloc));
    assert(!std::memcmp(before.get(),table.get(),sizeof(*table)) && buffer.data==published.data);
    assert(RebuildFleet(*table,buffer,initial,2,resolve,nullptr,std::calloc));
    assert(table->count==3 && table->slots[0].selected && !table->slots[2].selected);
    std::free(retained); std::free(published.data); std::free(buffer.data); buffer={};
    table->count=kFleetCapacity;
    const char* overflow[]={"synthetic_new"};
    *before=*table;
    assert(!RebuildFleet(*table,buffer,overflow,1,resolve,nullptr,std::calloc));
    assert(!std::memcmp(before.get(),table.get(),sizeof(*table)) && !buffer.data);
    const char* duplicates[]={"synthetic_alpha","SYNTHETIC_ALPHA"};
    assert(!RebuildFleet(*table,buffer,duplicates,2,resolve,nullptr,std::calloc));
    assert(!std::memcmp(before.get(),table.get(),sizeof(*table)));

    auto initialTable=std::make_unique<FleetTable>(); FleetBuffer initialBuffer{};
    const char* mixed[]={"synthetic_alpha","missing","synthetic_beta"};
    assert(RebuildFleet(*initialTable,initialBuffer,mixed,3,resolve,nullptr,std::calloc,&skipped));
    assert(skipped==1 && initialTable->count==2 && initialBuffer.count==2 && HasFleetEntitlements(initialBuffer));
    assert(!std::strcmp(initialTable->slots[1].name,"synthetic_beta"));
    assert(idAt(initialBuffer,1)==(kFleetUrnMarker|1));
    *before=*initialTable; const auto initialPublished=initialBuffer;
    const char* later[]={"synthetic_beta","missing","synthetic_gamma"};
    assert(!RebuildFleet(*initialTable,initialBuffer,later,3,resolve,nullptr,std::calloc));
    assert(!std::memcmp(before.get(),initialTable.get(),sizeof(*initialTable)) && initialBuffer.data==initialPublished.data);
    std::free(initialBuffer.data);

    auto invalidTable=std::make_unique<FleetTable>(); FleetBuffer invalidBuffer{};
    const char* invalid[]={"missing","null_class","zero_guid"};
    for(const char* name:invalid) {
        const char* one[]={name};
        assert(!RebuildFleet(*invalidTable,invalidBuffer,one,1,resolve,nullptr,std::calloc));
        assert(invalidTable->count==0 && !invalidBuffer.data);
    }
    const char* duplicateMissing[]={"missing","MISSING"};
    skipped=99;
    assert(!RebuildFleet(*invalidTable,invalidBuffer,duplicateMissing,2,resolve,nullptr,std::calloc,&skipped));
    assert(skipped==99 && invalidTable->count==0 && !invalidBuffer.data);
    assert(!RebuildFleet(*invalidTable,invalidBuffer,invalid,3,resolve,nullptr,failAlloc,&skipped));
    assert(skipped==99 && invalidTable->count==0 && !invalidBuffer.data);
    assert(!RebuildFleet(*invalidTable,invalidBuffer,invalid,3,resolve,nullptr,std::calloc,&skipped));
    assert(skipped==99 && invalidTable->count==0 && !invalidBuffer.data);
    assert(RebuildFleet(*invalidTable,invalidBuffer,missing,1,resolve,nullptr,std::calloc,&skipped));
    assert(skipped==1 && invalidTable->count==0 && invalidBuffer.count==0 && invalidBuffer.data);
    // Empty initial publication forwards the original failed entitlement response.
    assert(!HasFleetEntitlements(invalidBuffer));
    skipped=99;
    assert(!RebuildFleet(*invalidTable,invalidBuffer,invalid,3,resolve,nullptr,std::calloc,&skipped));
    assert(skipped==99 && invalidBuffer.count==0);
    std::free(invalidBuffer.data);
    std::puts("fleet transaction: stable slots, retained buffers, rollback and initial skip policy PASS");
}
