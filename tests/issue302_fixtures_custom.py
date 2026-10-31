"""Synthetic positive fixtures for bespoke issue 302 signature resolvers."""


def setup(filename: str) -> str:
    return FIXTURES.get(filename, "")


FIXTURES = {
    "actors_sigs.cpp": r'''
if(!strcmp(row.id,"spawn.landing_helper")){auto* s=parity302::str(img,0x100,"Landing Area could not be found.");auto* es=img.rdata.base+0x800;parity302::deps["teleport.entity_system"]=es;for(const auto& c:kHelperChecks)parity302::put(at+c.off,c.bytes);for(const auto& c:kHelperSlotChecks)parity302::put(at+c.off,c.bytes);for(auto off:kHelperEntitySystemLoads){parity302::put(at+off,"48 8B 0D");parity302::rel(at+off,3,7,es);}parity302::put(at+0x10a,"48 8B 0D");parity302::rel(at+0x10a,3,7,es+8);for(int i=0;i<kLandingAreaRefs;++i){auto* p=at+0x59+i*0x1000;parity302::put(p,"4C 8D 0D");parity302::rel(p,3,7,s);}fixture="custom:landing-helper-checks";exercise=true;}
if(!strcmp(row.id,"spawn.team_tag")){parity302::deps["spawn.landing_helper"]=at;fixture="custom:team-tag-offset";exercise=true;}
if(!strcmp(row.id,"spawn.seat_callback")){parity302::deps["spawn.find_seat"]=at;for(const auto& c:kSeatChecks)parity302::put(at+c.off,c.bytes);parity302::put(at+0x21c,"48 8D 05");parity302::rel(at+0x21c,3,7,at+0x800);for(const auto& c:kSeatCallbackChecks)parity302::put(at+0x800+c.off,c.bytes);fixture="custom:seat-callback";exercise=true;}
if(!strcmp(row.id,"spawn.seat_interactable")){auto* es=img.rdata.base+0x800;auto* fn=at+0x800;parity302::deps["spawn.seat_callback"]=at;parity302::deps["teleport.entity_system"]=es;parity302::put(at+kSeatGateCheck.off,kSeatGateCheck.bytes);parity302::rel(at+kSeatGateCheck.off+8,1,5,fn);for(const auto& c:kSeatInteractableChecks)parity302::put(fn+c.off,c.bytes);parity302::rel(fn+0x5b,3,7,es+8);parity302::rel(fn+0x62,3,7,parity302::str(img,0x100,kSeatInteractable));fixture="custom:seat-interactable-getter";exercise=true;}
if(!strcmp(row.id,"spawn.find_entity_by_name")){auto* s=parity302::str(img,0x100,"FindEntityByName_SlowDebugCodeOnly: %s");parity302::put(at,"48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40 48 C7 02 00 00 00 00");parity302::put(at+0x36,"48 8D 0D");parity302::rel(at+0x36,3,7,s);fixture="custom:find-entity-label";exercise=true;}
if(!strcmp(row.id,"spawn.toggle_flight_ready")){auto* s=parity302::str(img,0x100,"C:\\workspace\\CryEngine\\Code\\CryEngine\\CryCommon\\Events/ISC/Dashboards.h");auto* f=at+0x200;memset(f,0x90,0x40);parity302::put(f,"48 89 5C 24 08 48 8B FA 4C 89 44 24 20 41 B8 49 00 00 00");parity302::put(f+0x30,"48 8D 15");parity302::rel(f+0x30,3,7,s);fixture="custom:flight-ready-line";exercise=true;}
if(!strcmp(row.id,"spawn.fly_speed_scaler")){auto* s=parity302::str(img,0x100,"g_FlyModeSpeedScaler");parity302::put(at,"4C 8D 87 00 01 00 00");parity302::put(at+0x14,"48 8D 15");parity302::rel(at+0x14,3,7,s);fixture="custom:fly-speed-offset";exercise=true;}
if(!strcmp(row.id,"spawn.god_mode_call")){auto* s=parity302::str(img,0x100,"Actor [$$] has changed GodMode State from [$$] to [$$]");parity302::put(at,"4C 8D 0D");parity302::rel(at,3,7,s);parity302::put(at+0x64,"49 8B 8E 08 02 00 00 41 0F B6 D4 48 81 C1 F0 27 00 00 E8");fixture="custom:god-mode-label";exercise=true;}
if(!strcmp(row.id,"spawn.set_god_mode")){parity302::deps["spawn.god_mode_call"]=at;parity302::put(at+0x12,"E8");parity302::rel(at+0x12,1,5,at+0x200);parity302::put(at+0x200,"48 89 5C 24 08 57 48 83 EC 20 88 91");fixture="custom:god-mode-setter";exercise=true;}
if(!strcmp(row.id,"spawn.prefab_site")){auto* s=parity302::str(img,0x100,"ObjectContainers\\%s");auto* name=parity302::str(img,0x300,"ocFilename");for(const auto& c:kPrefabChecks)parity302::put(at+c.off,c.bytes);for(int i=0;i<kObjectContainersRefs;++i){auto* p=at+i*0x1000;parity302::put(p,"48 8D 15");parity302::rel(p,3,7,s);}parity302::rel(at+0x72,3,7,name);fixture="custom:prefab-labels";exercise=true;}
if(!strcmp(row.id,"npc.remove_entity")){parity302::deps["npc.direct_remove_call"]=at+0x100;parity302::put(at,kRemoveEntityPrologue);parity302::pdata(img,at);fixture="custom:remove-entity-function";exercise=true;}
if(!strcmp(row.id,"loadout.load_player_loadout")){auto* s=parity302::str(img,0x100,"Scripts/Loadouts/Player");for(const auto& c:kLoadoutChecks)parity302::put(at+c.off,c.bytes);parity302::put(at+0x44,"48 8D 15");parity302::rel(at+0x44,3,7,s);fixture="custom:loadout-loader-label";exercise=true;}
if(!strcmp(row.id,"loadout.port_container")){parity302::put(at,"40 53 57 48 83 EC 38 48 8D 05 ?? ?? ?? ?? 48 8B F9 48 89 44 24 50 48 8D 99 40 04 00 00 E8 ?? ?? ?? ?? 8B 50 18 39 53 08 75 05 FE 43 0C EB 2D BA 01 00 00 00");parity302::put(at+0xbb,"4C 8D 05");parity302::rel(at+0xbb,3,7,parity302::str(img,0x100,"IItemPortContainer"));fixture="custom:loadout-container-type";exercise=true;}
''',
    "asop_sigs.cpp": r'''
if(!strcmp(row.id,"asop.inventory_kind")){parity302::deps["asop.rm_request_deliver"]=at;fixture="custom:inventory-kind-offset";exercise=true;}
''',
    "gamerules_sigs.cpp": r'''
if(!strcmp(row.id,"gamerules.params_label")||!strcmp(row.id,"gamerules.params_sync")){
 auto* s=parity302::str(img,0x100,kParamsLabel);parity302::put(at,kSyncPrologue);parity302::put(at+0x80,"48 8D 05");parity302::rel(at+0x80,3,7,s);parity302::pdata(img,at);fixture="custom:gamerules-label-function";exercise=true;
}
''',
    "hangar_sigs.cpp": r'''
if(!strcmp(row.id,"hangar.request_instance")){auto* s=parity302::str(img,0x100,kRequesting);parity302::put(at,kRequestInstancePrologue);parity302::put(at+0xa9,"4C 8D 05");parity302::rel(at+0xa9,3,7,s);parity302::pdata(img,at);fixture="custom:request-instance-label";exercise=true;}
''',
    "datacore_live_sigs.cpp": r'''
if(!strcmp(row.id,"datacore_live.owner_vtable")){parity302::put(at,"48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56");parity302::put(at+0x40,"48 8D 05 ?? ?? ?? ?? 48 89 07 B9 58 00 00 00 48 89 AF 30 01 00 00");parity302::rel(at+0x40,3,7,img.rdata.base+0x100);parity302::pdata(img,at);fixture="custom:datacore-constructor-vtable";exercise=true;}
if(!strcmp(row.id,"datacore_live.owner_slot")){parity302::put(at,"48 8B 0D ?? ?? ?? ?? 4C 8D 45 C0 48 8D 55 D8 48 8B 01 FF 90 E8 00 00 00 48 8B 45 E0 33 F6");parity302::rel(at,3,7,img.rdata.base+0x100);fixture="custom:datacore-owner-load";exercise=true;}
if(!strcmp(row.id,"datacore_live.reset")||!strcmp(row.id,"datacore_live.deleting")){bool reset=!strcmp(row.id,"datacore_live.reset");auto* table=img.rdata.base+0x100;parity302::deps["datacore_live.owner_vtable"]=table;auto target=reinterpret_cast<uintptr_t>(at);memcpy(table+(reset?8:0x20),&target,8);if(reset){parity302::put(at,"4C 8B DC 53 56 57 48 81 EC 80 00 00 00");parity302::put(at+0x37,"C7 81 C8 01 00 00 03 00 00 00");}else parity302::put(at,"48 89 5C 24 08 57 48 83 EC 20 8B DA 48 8B F9 E8 ?? ?? ?? ?? F6 C3 01 74 0D BA D8 03 00 00");fixture="custom:datacore-vtable-method";exercise=true;}
if(!strcmp(row.id,"datacore_live.destructor")){auto* table=img.rdata.base+0x100;auto* fn=at+0x200;parity302::deps["datacore_live.owner_vtable"]=table;parity302::deps["datacore_live.deleting"]=at;parity302::put(at+0xf,"E8");parity302::rel(at+0xf,1,5,fn);parity302::put(fn,"48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8D 05 ?? ?? ?? ?? 48 8B D9 48 89 01 33 F6 48 8B 89 D0 01 00 00");parity302::rel(fn+0xf,3,7,table);fixture="custom:datacore-destructor-call";exercise=true;}
''',
    "entities_sigs.cpp": r'''
if(!strcmp(row.id,"entities.class_site")){parity302::deps["entities.dump"]=at;parity302::put(at+0x3b,"48 8D 05");parity302::rel(at+0x3b,3,7,at+0x200);for(const auto& c:kClassSiteChecks)parity302::put(at+0x200+c.off,c.bytes);fixture="custom:entity-class-lambda";exercise=true;}
if(!strcmp(row.id,"entities.spawn_sinks")){auto* s=parity302::str(img,0x100,"void __cdecl CEntitySystem::CallOnSpawnSinks(const class std::vector<class IEntityPtr,struct TempAllocator<class IEntityPtr> > &)");for(const auto& c:kSpawnSinksChecks)parity302::put(at+c.off,c.bytes);parity302::put(at+0x17,"48 8D 05");parity302::rel(at+0x17,3,7,s);parity302::pdata(img,at);fixture="custom:entity-spawn-sinks-label";exercise=true;}
if(!strcmp(row.id,"entities.delete_entity")){auto* s=parity302::str(img,0x100,"CEntitySystem::DeleteEntity");auto* s2=parity302::str(img,0x300,"void __cdecl CEntitySystem::DeleteEntity(class CEntityPtr)");for(const auto& c:kDeleteChecks)parity302::put(at+c.off,c.bytes);parity302::put(at+0x6c,"4C 8D 05");parity302::rel(at+0x6c,3,7,s);parity302::put(at+0x16b,"48 8D 05");parity302::rel(at+0x16b,3,7,s2);parity302::pdata(img,at);fixture="custom:entity-delete-labels";exercise=true;}
''',
    "events_sigs.cpp": r'''
if(!strcmp(row.id,"actor.is_dead_confirmed")||!strcmp(row.id,"event.player_death")||!strcmp(row.id,"event.seat_enter")||!strcmp(row.id,"event.seat_exit")){
 bool dead=!strcmp(row.id,"actor.is_dead_confirmed"), death=!strcmp(row.id,"event.player_death"), enter=!strcmp(row.id,"event.seat_enter");
 const char* label=dead?"CSCActorStatus::IsDeadConfirmed":death?"void __cdecl SCigEventDispatcher::QueueEvent<struct SPI_Player_OnDeath>(const struct SPI_Player_OnDeath &)":enter?"CSCActorResultStateLinked::Enter":"CSCActorResultStateLinked::Exit";
 auto* s=parity302::str(img,0x100,label);size_t off=death?0x414:0x80;parity302::put(at+off,dead||death?"48 8D 05":"4C 8D 05");parity302::rel(at+off,3,7,s);parity302::pdata(img,at);
 if(dead){parity302::deps["actor.state_site"]=at+0x800;parity302::put(at+0x80b,"E8");parity302::rel(at+0x80b,1,5,at);}
 if(death){parity302::put(at,"4C 89 44 24 18 48 89 54 24 10 55 53 57 41 54 41 55 41 57");parity302::put(at+0x22,"48 8B DA 4C 8B F9");parity302::put(at+0x69,"4D 8B 47 08");}
 if(enter)parity302::put(at,"48 89 5C 24 10 4C 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57");
 if(!dead&&!death&&!enter)parity302::put(at,"4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 53 56 57");
 fixture="custom:event-label-function";exercise=true;
}
''',
    "contracts_sigs.cpp": r'''
if(!strcmp(row.id,"contracts.mission_system_site")){parity302::deps["contracts.query_reply"]=at;parity302::put(at,"E8 ?? ?? ?? ?? 48 8B C8 E8");parity302::rel(at,1,5,at+0x200);parity302::rel(at+8,1,5,at+0x300);parity302::put(at+0x200,"48 8B 05 ?? ?? ?? ?? C3");parity302::put(at+0x300,"48 8B 41 10 C3");fixture="custom:mission-system-accessor-pair";exercise=true;}
if(!strcmp(row.id,"contracts.mission_system")){parity302::deps["contracts.mission_system_site"]=at;parity302::put(at,"E8");parity302::rel(at,1,5,at+0x200);parity302::put(at+0x200,"48 8B 05");parity302::rel(at+0x200,3,7,img.rdata.base+0x100);fixture="custom:mission-system-global";exercise=true;}
if(!strcmp(row.id,"contracts.accept_stub")){auto* s=parity302::str(img,0x100,"CContractBrokerOffline::AcceptContract is not implemented yet");parity302::put(at,"40 53 48 81 EC D0 00 00 00 48 8B DA 4C 8D 05");parity302::rel(at+0xc,3,7,s);fixture="custom:accept-stub-label";exercise=true;}
if(!strcmp(row.id,"contracts.accept_slot")){parity302::deps["contracts.accept_stub"]=at;auto target=reinterpret_cast<uintptr_t>(at);memcpy(img.rdata.base+0x100,&target,8);fixture="custom:accept-vtable-slot";exercise=true;}
if(!strcmp(row.id,"contracts.shard_store_site")){parity302::put(at+7,"E8");parity302::rel(at+7,1,5,at+0x200);parity302::put(at+0x200,"48 8B 81 00 00 00 00 C3");fixture="custom:shard-store-getter";exercise=true;}
if(!strcmp(row.id,"contracts.send_rewards_authority")){auto* s=parity302::str(img,0x100,"CSCPlayerMissionLog::SendRewards No authority");parity302::put(at,"FF 90 00 00 00 00 84 C0 0F 85");parity302::put(at+14,"4C 8D 05");parity302::rel(at+14,3,7,s);fixture="custom:rewards-authority-label";exercise=true;}
if(!strcmp(row.id,"contracts.add_player")){auto* fn=at+0x5000;parity302::deps["contracts.add_mission"]=fn;for(int i=0;i<kAddPlayerSites;++i){auto* p=at+i*0x40;parity302::put(p,"E8 ?? ?? ?? ?? 48 8D 90 80 00 00 00 E8");parity302::rel(p,1,5,fn);parity302::rel(p+12,1,5,at+0x6000);}fixture="custom:add-player-call-sites";exercise=true;}
if(!strcmp(row.id,"contracts.auto_accept_caller.1")||!strcmp(row.id,"contracts.auto_accept_caller.2")){auto* s=parity302::str(img,0x100,"DebugAcceptGeneratorContract AcceptContract(after creation) Sent");img.pdata={img.base+0x33000,12*kAutoAcceptCallers};for(int i=0;i<kAutoAcceptCallers;++i){auto* fn=at+i*0x100;parity302::put(fn+0x20,"48 8D 05");parity302::rel(fn+0x20,3,7,s);uint32_t e[3]={static_cast<uint32_t>(fn-img.base),static_cast<uint32_t>(fn-img.base+0x100),0x34000};memcpy(img.pdata.base+12*i,e,12);}img.base[0x34000]=1;fixture="custom:auto-accept-callers";exercise=true;}
''',
}
