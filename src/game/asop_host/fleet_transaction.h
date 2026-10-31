// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
namespace sco::game::asop_host {
constexpr size_t kFleetCapacity=2048, kFleetEntitlementSize=0x150;
constexpr uint64_t kFleetUrnMarker=0x53434F4600000000ull;
struct FleetSlot { char name[64]{}; uint64_t guid[2]{}; uintptr_t cls=0; bool selected=false; };
struct FleetTable { FleetSlot slots[kFleetCapacity]{}; size_t count=0; };
struct FleetBuffer { uint8_t* data=nullptr; size_t count=0; };
using FleetResolve=bool(*)(void*,const char*,uintptr_t&,uint64_t[2]);
using FleetAllocate=void*(*)(size_t,size_t);
inline bool FleetNameEqual(const char* a,const char* b) {
    while(*a && *b) {
        const auto lower=[](unsigned char c) { return c>='A' && c<='Z' ? c+('a'-'A') : c; };
        if(lower(*a++)!=lower(*b++)) return false;
    }
    return *a==*b;
}
// Caller serializes publication. Resolver must contain engine faults before returning.
// A published buffer is deliberately never freed: engine consumers can retain it.
inline bool RebuildFleet(FleetTable& table,FleetBuffer& buffer,const char* const* names,size_t count,
                         FleetResolve resolve,void* context,FleetAllocate allocate) {
    if(count>kFleetCapacity || (count && !names) || !resolve || !allocate) return false;
    auto next=std::unique_ptr<FleetTable>(new(std::nothrow) FleetTable(table));
    if(!next) return false;
    for(size_t i=0;i<next->count;++i) next->slots[i].selected=false;
    for(size_t i=0;i<count;++i) {
        if(!names[i]) return false;
        size_t length=0; while(length<64 && names[i][length]) ++length;
        if(!length || length==64) return false;
        size_t slot=0;
        while(slot<next->count && !FleetNameEqual(next->slots[slot].name,names[i])) ++slot;
        if(slot==next->count) {
            if(slot==kFleetCapacity) return false;
            ++next->count; std::memcpy(next->slots[slot].name,names[i],length+1);
        }
        auto& item=next->slots[slot];
        if(item.selected) return false;
        item.selected=true; item.cls=0; item.guid[0]=item.guid[1]=0;
        if(!resolve(context,names[i],item.cls,item.guid) || !item.cls || !(item.guid[0]|item.guid[1])) return false;
    }
    auto* bytes=static_cast<uint8_t*>(allocate(count?count:1,kFleetEntitlementSize));
    if(!bytes) return false;
    size_t written=0;
    for(size_t slot=0;slot<next->count;++slot) {
        const auto& item=next->slots[slot];
        if(!item.selected) continue;
        auto* entry=bytes+written++*kFleetEntitlementSize;
        const uint16_t type=0x1606; const uint64_t id=kFleetUrnMarker|slot; const uint32_t one=1;
        std::memcpy(entry,&type,sizeof(type)); entry[8]=3;
        std::memcpy(entry+0x10,&id,sizeof(id)); entry[0x20]=2; entry[0x50]=3;
        std::memcpy(entry+0x54,&one,sizeof(one)); std::memcpy(entry+0x58,item.guid,sizeof(item.guid));
    }
    // Existing name addresses/bytes remain immutable for FleetShipClass borrowers.
    for(size_t i=0;i<next->count;++i) {
        if(i>=table.count) std::memcpy(table.slots[i].name,next->slots[i].name,64);
        table.slots[i].cls=next->slots[i].cls; table.slots[i].selected=next->slots[i].selected;
        std::memcpy(table.slots[i].guid,next->slots[i].guid,sizeof(table.slots[i].guid));
    }
    table.count=next->count; buffer={bytes,written}; return true;
}
}
