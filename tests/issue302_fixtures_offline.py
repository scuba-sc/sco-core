"""Synthetic positive setups for the offline and feature signature resolvers.

Dependencies are address stubs, not evidence that the upstream rows resolve.
"""


def setup(filename: str) -> str:
    if filename == 'offline_sigs.cpp':
        return r'''
if (!strncmp(row.id,"offline.handshake_gate.",23)) {
    auto* s=parity302::str(img,0x100,"sessionGuid");
    for(int i=0;i<kHandshakeSites;++i) { auto* p=at+i*0x100;
        parity302::put(p,"44 38 05 ?? ?? ?? ?? 0F 84");parity302::rel(p,3,7,img.rdata.base+0x800);
        parity302::put(p+13,"48 8D 15");parity302::rel(p+13,3,7,s);
    } fixture="offline:four-handshake-gates";exercise=true;
}
if (!strcmp(row.id,"offline.megamap_tolower_call")) {
    auto* s=parity302::str(img,0x100,"Load a map, same usage as 'megamap' cvar.");
    parity302::put(at,"48 8D 15");parity302::rel(at,3,7,s);
    parity302::put(at+7,"4C 8D 05");parity302::rel(at+7,3,7,at+0x400);
    parity302::put(at+0x400,"48 8D 4C 24 30 E8");parity302::put(at+0x40a,"48 8D 4C 24 30 E8");
    parity302::rel(at+0x40f,1,5,at+0x800);parity302::put(at+0x800,"4C 8B 09 4C 8B D1 41 0F B6 01 84 C0");
    fixture="offline:megamap-registration-and-handler";exercise=true;
}
if (!strcmp(row.id,"offline.boot_frontend_request")) {
    auto* a=parity302::str(img,0x100,"Frontend_Main");auto* b=parity302::str(img,0x200,"SC_Frontend");
    parity302::put(at,"4C 8D 05");parity302::rel(at,3,7,b);parity302::put(at+7,"48 8D 15");parity302::rel(at+7,3,7,a);
    fixture="offline:frontend-paired-leas";exercise=true;
}
if (!strncmp(row.id,"offline.offline_db_path.",24)) {
    auto* s=parity302::str(img,0x100,"Libs/OfflineDB");
    for(int i=0;i<kOfflineDbSites;++i){auto* p=at+i*0x100;parity302::put(p,"4C 8D 05");parity302::rel(p,3,7,s);}
    fixture="offline:two-db-references";exercise=true;
}
if (!strcmp(row.id,"offline.use_service_default") || !strcmp(row.id,"offline.use_online_mission_service_default")) {
    auto* s=parity302::str(img,0x100,!strcmp(row.id,"offline.use_service_default")?kUseService:kUseOnlineMissions);
    parity302::put(at,"4C 8D 43");parity302::put(at+9,"41 B9 01 00 00 00");parity302::put(at+15,"48 8D 15");parity302::rel(at+15,3,7,s);
    fixture="offline:cvar-registration-default";exercise=true;
}
if (!strncmp(row.id,"offline.social_group.",21)) {
    for(int i=0;i<kSocialGroupSites;++i){auto* p=at+i*0x100;
        parity302::put(p,"48 8B 0D");parity302::rel(p,3,7,img.rdata.base+0x800);
        parity302::put(p+7,"48 8B 01 FF 50 18 48 8B 40 70 48 8B 80 B0 00 00 00");
    } fixture="offline:23-social-group-sites";exercise=true;
}
if (!strncmp(row.id,"offline.service_stream.",23)) {
    const char* service=!strcmp(row.id,"offline.service_stream.presence")?kPresence:!strcmp(row.id,"offline.service_stream.analytics")?kAnalytics:!strcmp(row.id,"offline.service_stream.trace")?kTrace:kEcho;
    char name[256];std::snprintf(name,sizeof(name),"auto __cdecl CAsyncClient<class sc::external::services::%s>::CreateClientStream::<lambda_3>::operator ()(const char *) const",service);
    auto* s=parity302::str(img,0x100,name);int n=service==kEcho?kEchoStreams:1;
    img.pdata={img.base+0x33000,static_cast<size_t>(n*12)};img.base[0x34000]=1;
    for(int i=0;i<n;++i){auto* p=at+i*0x300;parity302::put(p,"48 89 5C 24 18 55 56 57 41 56 41 57");
        parity302::put(p+0x80,"48 8D 15");parity302::rel(p+0x80,3,7,s);
        uint32_t entry[3]={static_cast<uint32_t>(p-img.base),static_cast<uint32_t>(p-img.base+0x200),0x34000};memcpy(img.pdata.base+i*12,entry,12);
    } fixture="offline:stream-functions-and-pdata";exercise=true;
}
if (!strcmp(row.id,"offline.remote_console_bind")) {
    auto* s=parity302::str(img,0x100,"Remote console listening on: %u\n");
    parity302::put(at,"33 C9 FF 15 ?? ?? ?? ?? 0F B7 CB 66 89 7C 24 48 89 44 24 4C");
    parity302::put(at+0x100,"48 8D 15");parity302::rel(at+0x100,3,7,s);
    fixture="offline:remote-console-unique-reference";exercise=true;
}
if (!strcmp(row.id,"fleet.takeoff_command")) {
    auto* s=parity302::str(img,0x100,"Invalid arguments. Usage: g_ATC_requestTakeOff <atc_name (autocompletable)> [<ship_archetype>] [<pad_name_filter>]");
    parity302::put(at,"40 55 53 48 8B EC 48 83 EC 78");at[0x72]=at[0xa9]=at[0x190]=0xE8;
    parity302::put(at+0x1cd,"48 8D 15");parity302::rel(at+0x1cd,3,7,s);
    fixture="offline:takeoff-command-usage";exercise=true;
}
if (!strcmp(row.id,"system.quit_fast_shutdown")) {
    parity302::deps["system.quit"]=at;parity302::put(at+0x31a,"49 8B 8E ?? ?? ?? ?? 48 85 C9 74 0A 48 8B 01 FF 50 10 85 C0 75 0E 41 80 BE ?? ?? ?? ?? 00 0F 84");
    fixture="offline:quit-dependency-stub";exercise=true;
}
'''
    if filename == 'features_sigs.cpp':
        return r'''
if (!strcmp(row.id,"spawn.request_fly_mode")) {
    auto* s=parity302::str(img,0x100,"unsigned short __cdecl CSCActorActionHandler::Request<struct SCActorActionHandlerActions::SFlyMode,const enum ESCActorFlyMode&>(const enum ESCActorFlyMode &)");
    parity302::put(at,"89 54 24 10 48 83 EC 28 48 8D 54 24 38 E8");parity302::rel(at,0xe,0x12,at+0x400);
    parity302::put(at+0x400+0xae,"48 8D 05");parity302::rel(at+0x400+0xae,3,7,s);
    fixture="features:fly-wrapper-and-label";exercise=true;
}
if (!strcmp(row.id,"npc.remove_entity_call")) {
    parity302::deps["teleport.entity_system"]=img.rdata.base+0x800;
    parity302::put(at,"48 8B 0D ?? ?? ?? ?? 48 8B 13 48 8B 01 FF 90 ?? ?? ?? ?? 48 83 C3 08 48 3B DF 75 E4");
    parity302::rel(at,3,7,img.rdata.base+0x800);int32_t slot=0x80;memcpy(at+kRemoveSlotDisp,&slot,4);
    fixture="features:remove-entity-dependency-stub";exercise=true;
}
if (!strcmp(row.id,"quantum.send_effect_tag")) {
    auto* s=parity302::str(img,0x100,"C:\\workspace\\CryEngine\\Code\\CryEngine\\CryCommon\\Events/VFX/EntityEffectSystem.h");
    parity302::put(at,"48 89 5C 24 08 57 48 83 EC 50 8B 05 ?? ?? ?? ?? 48 8B FA 4C 89 44 24 20 48 8B D9 85 C0 75 19 41 B8 17 00 00 00 48 8D 15");
    parity302::rel(at+0x25,3,7,s);fixture="features:effect-sender-header";exercise=true;
}
if (!strcmp(row.id,"contracts.reputation_services")) {
    auto* s=parity302::str(img,0x100,"Couldn't access reputation service internal");
    parity302::put(at,kServicesLoad);parity302::rel(at,3,7,img.rdata.base+0x800);
    parity302::put(at+0x80,"4C 8D 05");parity302::rel(at+0x80,3,7,s);
    fixture="features:reputation-services-message";exercise=true;
}
if (!strncmp(row.id,"contracts.reputation_check.",27)) {
    parity302::deps["contracts.reputation_services"]=img.rdata.base+0x800;
    for(int i=0;i<kReputationChecks;++i){auto* p=at+i*0x100;parity302::put(p,kReputationForms[i%2]);parity302::rel(p,3,7,img.rdata.base+0x800);}
    fixture="features:ten-reputation-checks-dependency-stub";exercise=true;
}
if (!strncmp(row.id,"offline.or_loop_bound.",22)) {
    for(int i=0;i<kOrLoopSites;++i) parity302::put(at+i*0x100,"44 8B A4 24 E8 00 00 00 8B 8C 24 D8 00 00 00 FF C3 48 FF C6 49 81 C7 90 00 00 00 48 3B 74 24 68 0F 8C ?? ?? ?? ?? 4C 8D 77 78 89 6F 08 41 8B 45 18");
    fixture="features:four-or-loop-sites";exercise=true;
}
'''
    return ''
