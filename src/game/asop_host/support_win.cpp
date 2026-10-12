// SPDX-License-Identifier: GPL-3.0-only
// Narrow adapters to existing guarded game-pack primitives; no product symbols.
#include "engine.h"
#include "../services/spawn.h"
namespace sco::game::asop_host {
uint64_t EntityIdOfHandle(const void* handle) {
    if (!handle || !sco::SigReady("spawn.handle_to_id")) return 0;
    __try {
        using Fn=uint64_t*(__fastcall*)(const void*,uint64_t*);
        uint64_t id=0; reinterpret_cast<Fn>(sco::Sig("spawn.handle_to_id"))(handle,&id); return id;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
uint64_t EntityIdOfComponent(uintptr_t c) { return c ? EntityIdOfHandle(reinterpret_cast<const void*>(c+8)) : 0; }
uint64_t LocalPlayerEntityId() {
    __try { uintptr_t a=0,e=0; return reads::LocalPlayer(a,e) ? EntityIdOfHandle(reinterpret_cast<const void*>(a+8)) : 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool EntityAlive(uint64_t id) {
    __try { return reads::EntityFromId(id)!=0; } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool FindEntityByNameEx(const char* name,uintptr_t& entity,uint64_t& id) {
    entity=0; id=0;
    if (!name || !*name || !sco::SigReady("spawn.find_entity_by_name")) return false;
    __try {
        using Fn=uint64_t*(__fastcall*)(uintptr_t,uint64_t*,const char*);
        uint64_t h=0;
        if(!EntitySystem()) return false;
        reinterpret_cast<Fn>(sco::Sig("spawn.find_entity_by_name"))(EntitySystem(),&h,name);
        entity=h & kPtrMask;
        if(entity) id=EntityIdOfHandle(&h);
        return entity!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { entity=0; return false; }
}
const char* SpawnEntityNearPlayer(const char* cls,const double offset[3],uint64_t& id) {
    uint64_t zoneId=0; double pos[3]={};
    __try {
        uintptr_t actor=0,entity=0;
        if(!reads::LocalPlayer(actor,entity)) return "player unavailable";
        const uintptr_t zone=reads::EntityZone(entity);
        if(!zone) return "zone unavailable";
        zoneId=reads::ZoneId(zone);
        if(!zoneId || reads::ZoneFromId(zoneId)!=zone) return "zone lookup mismatch";
        reads::EntityLocalPos(entity,pos);
        for(int i=0;i<3;++i) pos[i]+=offset[i];
    } __except(EXCEPTION_EXECUTE_HANDLER) { return "player pose fault"; }
    return services::SpawnEntityInZone(cls,zoneId,pos,id);
}
// Only the internal delivery setup cvar, never an arbitrary console command.
bool GetCVarNow(const char* name,float& value) {
    if(std::strcmp(name,"g_itemRecovery.deliverySystemSetup") || !sco::SigReady("cvars.console")) return false;
    __try {
        const auto* slot=reinterpret_cast<uintptr_t*>(sco::Sig("cvars.console"));
        const uintptr_t cvar=*slot ? VCall<uintptr_t>(*slot,0xC0,name) : 0;
        if(!cvar) return false; value=VCall<float>(cvar,0x20); return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SetCVarNow(const char* name,float value) {
    if(std::strcmp(name,"g_itemRecovery.deliverySystemSetup") || !sco::SigReady("cvars.console")) return false;
    __try {
        const auto* slot=reinterpret_cast<uintptr_t*>(sco::Sig("cvars.console"));
        const uintptr_t cvar=*slot ? VCall<uintptr_t>(*slot,0xC0,name) : 0;
        if(!cvar) return false; VCall<void>(cvar,0x38,value); return VCall<float>(cvar,0x20)==value;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
} // namespace sco::game::asop_host
