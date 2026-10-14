// SPDX-License-Identifier: GPL-3.0-only
// Project-authored synthetic no-game fixture over real opened OS mappings.
#include "ipc/diagnostics_ipc.h"
#include "sco/caps.h"
#include "sco/host.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#endif
using namespace sco::diagnostics;
namespace di=sco::diagnostics::ipc;
namespace os=sco::diagnostics::platform;
void RunDiagnosticsJsonTests();
#define CHECK(x) do {if(!(x)){std::cerr<<__LINE__<<": "<<#x<<"\n";std::abort();}}while(0)
// Test setup, not a relaxation of the production credential policy. Windows
// filesystem::permissions only changes readonly attributes, not the directory
// DACL. Match the current-token protected directory used by the platform test.
// MSVC's std::filesystem::canonical() yields an extended-length path ("\\?\C:\...").
// The credential transport accepts plain DOS drive paths by contract and generates
// that prefix itself, so hand it the plain spelling it documents; every check stays.
static std::string PlainFixturePath(const std::filesystem::path& p) {
    std::string s = p.string();
    if (s.rfind("\\\\?\\", 0) == 0) s.erase(0, 4);
    return s;
}
static void CreatePrivateFixtureDirectory(const std::filesystem::path& dir) {
#ifdef _WIN32
    HANDLE token = nullptr;
    CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    CHECK(bytes > 0);
    std::vector<unsigned char> tokenInfo(bytes);
    CHECK(GetTokenInformation(token, TokenUser, tokenInfo.data(), bytes, &bytes));
    CHECK(CloseHandle(token));
    LPWSTR sid = nullptr;
    CHECK(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(tokenInfo.data())->User.Sid, &sid));
    const std::wstring ownSid(sid);
    LocalFree(sid);
    const std::wstring sddl = L"O:" + ownSid + L"D:P(A;;FA;;;" + ownSid + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    const BOOL created = CreateDirectoryW(dir.c_str(), &attributes);
    const DWORD error = created ? ERROR_SUCCESS : GetLastError();
    LocalFree(descriptor);
    if (!created) std::cerr << "private fixture directory failed: " << error << '\n';
    CHECK(created);
#else
    CHECK(std::filesystem::create_directory(dir));
    std::filesystem::permissions(dir, std::filesystem::perms::owner_all);
#endif
}
#define CHECK_RESULT(expression, expected) do { \
    const auto actualResult = (expression); \
    if (actualResult != (expected)) std::cerr << "Start result=" << static_cast<int>(actualResult) \
        << ", expected=" << static_cast<int>(expected) << '\n'; \
    CHECK(actualResult == (expected)); \
} while (0)

struct Sink final : di::FixtureAudit {
    unsigned events=0; bool accept=true;
    bool Acknowledge(const di::AuditEvent&) override {++events;return accept;}
};
std::string root,path;
std::vector<uint8_t> Bytes(const char* hex) {std::vector<uint8_t>b(std::strlen(hex)/2);CHECK(di::Unhex(hex,b.data(),b.size()));return b;}
void Vectors() {
    // Independent design offsets + Python stdlib HMAC (not this header/implementation).
    const auto literal=Bytes("5347443101000001000102030405060708090a0b0c0d0e0f08070605040302010200000000000000101112131415161718191a1b1c1d1e1f0200000001000000e8030000000000007b7d3f2d17afcde2aaa0e7c71bf6c4fe7ea5244f13359faada81575e0200f3ce82c3");
    di::Frame f;f.opcode=0x100;f.epoch=0x0102030405060708;f.sequence=2;f.deadline=1000;f.direction=1;f.payload="{}";
    di::Credentials c;di::Key client{},host{},peerKey{},hostKey{};
    for(unsigned i=0;i<32;++i){c.secret[i]=uint8_t(i);client[i]=uint8_t(i);host[i]=uint8_t(i+32);}
    for(unsigned i=0;i<16;++i){f.session[i]=uint8_t(i);f.request[i]=uint8_t(i+16);}
    c.session=f.session;c.epoch=f.epoch;
    CHECK(di::Encode(f,c.secret)==literal);di::Frame parsed;
    CHECK(di::Decode(literal.data(),literal.size(),c.secret,parsed,4096)==di::Result::Ok);
    CHECK(parsed.opcode==0x100&&parsed.sequence==2&&parsed.payload=="{}");
    di::Derive(c,client,host,peerKey,hostKey);
    CHECK(di::Hex(peerKey.data(),32)=="9b6dd1a1c524dd3b80e0410473e560b52777345fe6fa78f11d7e602003d92135");
    CHECK(di::Hex(hostKey.data(),32)=="38cddb82777d43d0e5b392732f935842c01627279735569f92d830e4676adf7e");
    auto wrong=literal;wrong.back()^=1;CHECK(di::Decode(wrong.data(),wrong.size(),c.secret,parsed,4096)==di::Result::Auth);
    for(size_t n=0;n<104;++n)CHECK(di::Decode(literal.data(),n,c.secret,parsed,4096)==di::Result::Corrupt);
    for(size_t index:{size_t(0),size_t(4),size_t(56),size_t(61)}){wrong=literal;wrong[index]^=1;CHECK(di::Decode(wrong.data(),wrong.size(),c.secret,parsed,4096)==di::Result::Corrupt);}
    f.payload=std::string(3992,' ');CHECK(di::Encode(f,c.secret).size()==4096);f.payload+=' ';CHECK(di::Encode(f,c.secret).empty());
}
struct Fixture {
    SnapshotStore snapshots;Capture capture;Sink sink;di::Host host;di::Options options;
    std::vector<sco::plugins::Plugin> plugins;
    Fixture():host(snapshots,capture){options.enabled=true;options.credentialPath=path;options.excludedRoots={root};options.hostBuild="fixture-host-1";
        HostMetadata metadata;metadata.hostBuild="fixture-host-1";metadata.readiness="ready";
        LifecycleMetadata lifecycle;lifecycle.lifecycle="ready";lifecycle.offlineAdmission="authorized";
        plugins.resize(1);plugins[0].manifestOk=true;plugins[0].manifest.id="fixture_ipc";plugins[0].manifest.version="1.0";
        plugins[0].manifest.kind=sco::plugins::Kind::Native;plugins[0].state=sco::plugins::State::Loaded;
        CHECK(snapshots.Begin(plugins,metadata,lifecycle,1000));size_t steps=0;while(!snapshots.Advance(1000))CHECK(++steps<2048);
        CHECK(capture.Start(1000));CHECK(capture.AssignKey(this,"fixture_ipc"));capture.RecordHost(this,HostEvent::Loaded,1000);
        CHECK_RESULT(host.Start(options,&sink,1000),di::Result::Ok);
    }
    ~Fixture(){host.Stop();}
};
struct Raw {
    os::Mapping mapping;os::Lease lease;di::Credentials c;sc_ipc_chan channel{};sc_ipc_ring_view request{},response{};
    di::Key client{},host{},peerKey{},hostKey{};di::Id id{};uint64_t sequence=2;
    Raw(){std::string text;CHECK(os::ReadCredential(path,text)&&di::ParseCredential(text,c));CHECK(os::Acquire(lease));CHECK(os::Open(mapping));
        CHECK(di::Geometry(mapping,c,channel,request,response,1000)==di::Result::Ok);sc_ipc_peer_beat(&channel,0,1000);client.fill(0x11);id.fill(0x22);}
    ~Raw(){os::Close(mapping);os::Release(lease);}
    di::Frame Make(uint16_t opcode,std::string payload,uint64_t now=1000){di::Frame f;f.opcode=opcode;f.session=c.session;f.epoch=c.epoch;f.sequence=sequence++;f.request=id;
        f.deadline=now+2000;f.direction=1;f.payload=std::move(payload);return f;}
    void Push(const di::Frame& f,const di::Key& key,uint32_t type=1){auto b=di::Encode(f,key);CHECK(!b.empty());CHECK(sc_ipc_ring_push(&request,type,b.data(),uint32_t(b.size()))==SC_IPC_OK);}
    di::Frame Pop(const di::Key& key){std::array<uint8_t,65536>b{};uint32_t n=65536,type=0;CHECK(sc_ipc_ring_pop(&response,&type,b.data(),&n)==SC_IPC_OK&&type==1);di::Frame f;CHECK(di::Decode(b.data(),n,key,f,65536)==di::Result::Ok);return f;}
    di::Frame Hello(){auto f=Make(1,"{\"client_nonce\":\""+di::Hex(client.data(),32)+"\",\"expected_host_build\":\"fixture-host-1\"}");f.sequence=0;sequence=2;return f;}
    void Connect(Fixture& fixture){Push(Hello(),c.secret);CHECK(fixture.host.Poll(1000)==di::Result::Ok);auto challenge=Pop(c.secret);
        json::Object o;CHECK(json::Parse(challenge.payload,o)&&json::Control(2,o));CHECK(di::Unhex(o.strings.at("host_nonce"),host.data(),32));
        di::Derive(c,client,host,peerKey,hostKey);auto finish=Make(3,"{\"client_nonce\":\""+di::Hex(client.data(),32)+"\",\"host_nonce\":\""+di::Hex(host.data(),32)+"\"}");
        finish.sequence=1;sequence=2;Push(finish,peerKey);CHECK(fixture.host.Poll(1000)==di::Result::Ok);CHECK(Pop(hostKey).opcode==4);}
};
void DisabledAndCredential() {
    SnapshotStore s;Capture c;Sink sink;di::Host h(s,c);di::Options o;o.credentialPath=path;
    CHECK(h.Start(o,&sink,1000)==di::Result::Disabled);CHECK(!std::filesystem::exists(path));
    di::Peer p;CHECK(p.Begin(path,1000)==di::Result::Auth);
    // A null fixture sink selects production admission: policy defaults refuse
    // first, but an authorized product must still fail closed without a collector.
    o.enabled=true;o.excludedRoots={root};CHECK(h.Start(o,nullptr,1000)==di::Result::Disabled);
    CHECK(!h.Started()&&!std::filesystem::exists(path));
    o.policy.enabled=o.policy.disclosureConsent=true;
    o.policy.admission=std::make_shared<std::atomic<OfflineAdmission>>(OfflineAdmission::Authorized);
    CHECK(h.Start(o,nullptr,1000)==di::Result::Audit);
    CHECK(!h.Started()&&!std::filesystem::exists(path));
    o.credentialPath=root+"/credential.json";CHECK(h.Start(o,&sink,1000)==di::Result::Unavailable);
    {
        Fixture f;di::Credentials credentials;std::string text;CHECK(os::ReadCredential(path,text));CHECK(di::ParseCredential(text,credentials));
        for(auto bad:{text+"x",std::string("{}"),text.substr(0,text.size()-1)+",\"extra\":1}"})CHECK(!di::ParseCredential(bad,credentials));
        auto old=text;os::RemoveCredential(path);CHECK(os::WriteCredential(path,"{}"));CHECK(p.Begin(path,1000)==di::Result::Auth);
        os::RemoveCredential(path);CHECK(os::WriteCredential(path,old));
        // Exclusive creation cannot remove a leftover credential.
        f.host.Stop();CHECK(os::WriteCredential(path,old));CHECK(f.host.Start(f.options,&f.sink,1000)==di::Result::Unavailable);
        CHECK(os::ReadCredential(path,text)&&text==old);os::RemoveCredential(path);
    }
}
void GeometryAttacks() {
    struct Attack { size_t offset; uint8_t value; };
    const std::vector<Attack> attacks{{4,2},{32,1},{48,0},{52,2},{60,1},{64,0},{68,2},{70,0},{88,0xff},{96,0xff},{104,1},{112,1},
        {132,1},{136,0xff},{152,1},{33092,2},{33104,1},{33112,1},{164352,1}};
    for(auto [offset,value]:attacks){Fixture f;os::Mapping m;CHECK(os::Open(m));
        // Some targets hold per-run random bytes (ring epochs): always change the byte.
        uint8_t* target=static_cast<uint8_t*>(m.base)+offset;const uint8_t before=sc_ipc_i_ld8r(target);
        sc_ipc_i_st8r(target,before==value?uint8_t(~value):value);CHECK(sc_ipc_i_ld8r(target)!=before);
        di::Peer p;CHECK(p.Begin(path,1000)!=di::Result::Ok);CHECK(f.host.Poll(1000)!=di::Result::Ok);os::Close(m);}
    for(unsigned kind=0;kind<5;++kind){Fixture f;Raw r;auto* ring=r.request.ring;
        if(kind==0)sc_ipc_i_st64(&ring->head,1);
        if(kind==1)sc_ipc_i_st64(&ring->head,32776);
        if(kind==2)sc_ipc_i_st64(&ring->pushed,9);
        if(kind==3)sc_ipc_i_st64(&ring->popped,1);
        if(kind==4)sc_ipc_i_st64(&ring->head,UINT64_MAX-7);
        CHECK(f.host.Poll(1000)==di::Result::Corrupt);}
    {Fixture f;Raw r;sc_ipc_i_st64(&sc_ipc_i_hdr(&r.channel)->peer_heartbeat_ms,1001);CHECK(f.host.Poll(1000)==di::Result::Corrupt);}
    {Fixture f;Raw r;sc_ipc_i_st64(&sc_ipc_i_hdr(&r.channel)->owner_heartbeat_ms,1001);di::Peer p;CHECK(f.host.Poll(1000)==di::Result::Corrupt);}
    {Fixture f;Raw r;CHECK(f.host.Poll(6001)==di::Result::Deadline);}
    // Descriptor complete literal, including session bytes independently substituted from credential.
    {Fixture f;Raw r;auto expected=Bytes("5347443101004000000102030405060708090a0b0c0d0e0f80000000000000004081000000000000001000000000010000000000000000000000000000000000");
        std::memcpy(expected.data()+8,r.c.session.data(),16);std::array<uint8_t,64>actual{};sc_ipc_i_copy_out(actual.data(),static_cast<uint8_t*>(r.mapping.base)+64,64);
        CHECK(std::equal(actual.begin(),actual.end(),expected.begin()));}
}
void HostileFrames() {
    for(unsigned kind=0;kind<12;++kind){Fixture f;Raw r;auto hello=r.Hello();auto key=r.c.secret;
        if(kind==0)key[0]^=1;
        if(kind==1)hello.sequence=1;
        if(kind==2)hello.epoch++;
        if(kind==3)hello.session[0]^=1;
        if(kind==4)hello.deadline=999;
        if(kind==5)hello.deadline=3001;
        if(kind==6)hello.direction=2;
        if(kind==7)hello.request.fill(0);
        if(kind==8)hello.payload="{\"client_nonce\":\""+std::string(64,'0')+"\",\"expected_host_build\":\"fixture-host-1\"}";
        if(kind==9)hello.payload="{}";
        if(kind==10)hello.opcode=0x100;
        if(kind==11)hello.payload="{\"client_nonce\":\""+std::string(64,'1')+"\",\"expected_host_build\":\"wrong-host\"}";
        r.Push(hello,key);CHECK(f.host.Poll(1000)!=di::Result::Ok);CHECK(!f.host.Started());}
    for(unsigned kind=0;kind<5;++kind){Fixture f;Raw r;auto b=di::Encode(r.Hello(),r.c.secret);
        if(kind==0)b[56]++;
        if(kind==1)b[61]=1;
        if(kind==2)b.resize(103);
        if(kind==3)b.push_back(0);
        if(kind==4)b.resize(4097);
        CHECK(sc_ipc_ring_push(&r.request,1,b.data(),uint32_t(b.size()))==SC_IPC_OK);CHECK(f.host.Poll(1000)!=di::Result::Ok);}
    {Fixture f;Raw r;r.Push(r.Hello(),r.c.secret,2);CHECK(f.host.Poll(1000)==di::Result::Corrupt);}
    {Fixture f;Raw r;r.Push(r.Hello(),r.c.secret);CHECK(f.host.Poll(1000)==di::Result::Ok);r.Pop(r.c.secret);CHECK(f.host.Poll(3001)==di::Result::Deadline);}
    for(auto payload:{std::string("{\"x\":\"\xc0\xaf\"}"),std::string("{\"x\":1,\"x\":2}"),std::string("{\"x\":NaN}"),std::string("{}junk"),std::string(17,'[')+"0"+std::string(17,']'),std::string("{\"x\":\"\\ud800\"}")}){
        Fixture f;Raw r;r.Connect(f);r.Push(r.Make(0x100,payload),r.peerKey);CHECK(f.host.Poll(1000)==di::Result::Corrupt);}
    for(unsigned kind=0;kind<3;++kind){Fixture f;Raw r;r.Connect(f);auto request=r.Make(0x100,"{}");if(kind==0)request.sequence=1;if(kind==1)request.sequence=3;if(kind==2)request.epoch++;
        r.Push(request,r.peerKey);CHECK(f.host.Poll(1000)!=di::Result::Ok);}
    {Fixture f;Raw r;r.Connect(f);auto old=r.Make(0x100,"{}");r.Push(old,r.peerKey);CHECK(f.host.Poll(1000)==di::Result::Ok);r.Pop(r.hostKey);r.Push(old,r.peerKey);CHECK(f.host.Poll(1000)==di::Result::Auth);}
    {Fixture f;Raw r;r.Connect(f);r.Push(r.Make(5,"{\"reason\":\"client_exit\"}"),r.peerKey);CHECK(f.host.Poll(1000)==di::Result::Ok);
        r.Push(r.Hello(),r.c.secret);CHECK(f.host.Poll(1000)==di::Result::Auth);}
    {Fixture f;Raw r;r.Connect(f);r.Push(r.Make(0x100,"{}"),r.peerKey);r.Push(r.Make(0x100,"{}"),r.peerKey);
        CHECK(f.host.Poll(1000)==di::Result::Ok);CHECK(f.host.Poll(1000)==di::Result::Busy);}
}
void PeerChecks() {
    for(unsigned kind=0;kind<6;++kind){Fixture f;di::Peer p;CHECK(p.Begin(path,1000)==di::Result::Ok);CHECK(f.host.Poll(1000)==di::Result::Ok);
        os::Mapping m;CHECK(os::Open(m));std::string text;di::Credentials c;CHECK(os::ReadCredential(path,text)&&di::ParseCredential(text,c));
        sc_ipc_chan channel{};sc_ipc_ring_view request{},response{};CHECK(di::Geometry(m,c,channel,request,response,1000)==di::Result::Ok);
        auto pos=sc_ipc_i_ld64(&response.ring->tail)&(response.capacity-1);sc_ipc_rec record{};sc_ipc_i_copy_out(&record,response.data+pos,8);
        std::vector<uint8_t>b(record.size);sc_ipc_i_copy_out(b.data(),response.data+pos+8,b.size());di::Frame challenge;CHECK(di::Decode(b.data(),b.size(),c.secret,challenge,65536)==di::Result::Ok);
        json::Object o;CHECK(json::Parse(challenge.payload,o));di::Key client{},host{},peerKey{},hostKey{};CHECK(di::Unhex(o.strings.at("client_nonce"),client.data(),32)&&di::Unhex(o.strings.at("host_nonce"),host.data(),32));
        di::Derive(c,client,host,peerKey,hostKey);
        CHECK(p.Challenge(1000)==di::Result::Ok);CHECK(f.host.Poll(1000)==di::Result::Ok);CHECK(p.Ready(1000)==di::Result::Ok);
        di::Peer second;CHECK(second.Begin(path,1000)==di::Result::Busy);CHECK(p.Send(0x100,"{}",1000)==di::Result::Ok);CHECK(p.Send(0x100,"{}",1000)==di::Result::Busy);CHECK(f.host.Poll(1000)==di::Result::Ok);
        pos=sc_ipc_i_ld64(&response.ring->tail)&(response.capacity-1);sc_ipc_i_copy_out(&record,response.data+pos,8);b.resize(record.size);sc_ipc_i_copy_out(b.data(),response.data+pos+8,b.size());
        di::Frame reply;CHECK(di::Decode(b.data(),b.size(),hostKey,reply,65536)==di::Result::Ok);
        if(kind==0)b.back()^=1;
        else {if(kind==1)reply.request[0]^=1;if(kind==2)reply.sequence++;if(kind==3)reply.opcode++;if(kind==4)reply.epoch++;if(kind==5)reply.deadline--;b=di::Encode(reply,hostKey);}
        CHECK(b.size()==record.size);sc_ipc_i_copy_in(response.data+pos+8,b.data(),b.size());std::string output="not-cleared";
        CHECK(p.Receive(1000,output)!=di::Result::Ok&&output.empty());os::Close(m);
    }
    {Fixture f;di::Peer p;CHECK(p.Begin(path,1000)==di::Result::Ok);CHECK(f.host.Poll(1000)==di::Result::Ok);
        os::Mapping m;CHECK(os::Open(m));auto* hdr=static_cast<sc_ipc_hdr*>(m.base);sc_ipc_i_st64(&hdr->epoch,sc_ipc_i_ld64(&hdr->epoch)+1);
        CHECK(p.Challenge(1000)==di::Result::Epoch);os::Close(m);}
    {Fixture f;di::Peer p;CHECK(p.Begin(path,1000)==di::Result::Ok);CHECK(f.host.Poll(1000)==di::Result::Ok);CHECK(p.Challenge(3001)==di::Result::Deadline);}
}
void ReadsAndNoSideEffects() {
    static const int owner=0;unsigned calls[4]{};unsigned index=0;
    const char* names[]{"fixture.raw","fixture.mutation","fixture.reload","fixture.network"};
    for(auto name:names){sco::Command command{name,"Fixture",nullptr,nullptr,nullptr,0,
        [](const sco::Arg*,uint32_t,void* ctx,char*,uint32_t){++*static_cast<unsigned*>(ctx);return sco::Result::Ok;},&calls[index++]};
        CHECK(sco::RegisterCommand(&owner,"fixture",command)==sco::Result::Ok);}
    static const uint8_t table=0;CHECK(sco::ProvideService(&owner,"fixture","fixture.side_effect",0x10000,&table)==sco::Result::Ok);
    CHECK(!sco::ServiceTableHandedOut(&owner));CHECK(sco::caps::Set("fixture.ipc_cap",true)==sco::Result::Ok);
    {Fixture f;di::Peer p;CHECK(p.Begin(path,1000)==di::Result::Ok);CHECK(f.host.Poll(1000)==di::Result::Ok);
        CHECK(p.Challenge(1000)==di::Result::Ok);CHECK(f.host.Poll(1000)==di::Result::Ok);CHECK(p.Ready(1000)==di::Result::Ok);
        for(unsigned i=0;i<6;++i){CHECK(p.Send(0x100,"{}",1000)==di::Result::Ok);CHECK(f.host.Poll(1000)==di::Result::Ok);std::string output;
            CHECK(p.Receive(1000,output)==di::Result::Ok);CHECK(output.find(i==5?"rate_limited":"\"host_version\"")!=std::string::npos);}
        p.Close();CHECK(f.host.Poll(1000)==di::Result::Ok);}
    {
        Fixture f;Raw r;r.Connect(f);const auto snapshot=f.snapshots.Latest();
        const char* inputs[]{"{}","{}","{\"plugin_id\":\"fixture_ipc\"}","{}","{}","{}"};
        for(uint16_t op=0x100;op<=0x105;++op){const uint64_t now=op==0x105?3000:2000;r.Push(r.Make(op,inputs[op-0x100],now),r.peerKey);sc_ipc_peer_beat(&r.channel,0,now);
            CHECK(f.host.Poll(now)==di::Result::Ok);auto response=r.Pop(r.hostKey);CHECK(response.opcode==op&&response.payload.find("\"data\":null")==std::string::npos);}
        CHECK(f.snapshots.Latest()==snapshot&&f.plugins[0].state==sco::plugins::State::Loaded&&sco::caps::Has("fixture.ipc_cap"));
        for(uint16_t op:{uint16_t(0x200),uint16_t(0x201),uint16_t(0x202),uint16_t(0x203),uint16_t(0xffff)}){
            r.Push(r.Make(op,"{\"name\":\"fixture.raw\"}",3000),r.peerKey);sc_ipc_peer_beat(&r.channel,0,3000);CHECK(f.host.Poll(3000)==di::Result::Ok);
            CHECK(r.Pop(r.hostKey).payload.find("invalid_request")!=std::string::npos);}
        for(auto invalid:{"{\"path\":\"fixture\"}","{\"limit\":51}","{\"cursor\":null}"}){r.Push(r.Make(0x101,invalid,3000),r.peerKey);CHECK(f.host.Poll(3000)==di::Result::Ok);CHECK(r.Pop(r.hostKey).payload.find("invalid_request")!=std::string::npos);}
        f.sink.accept=false;r.Push(r.Make(0x100,"{}",4000),r.peerKey);sc_ipc_peer_beat(&r.channel,0,4000);CHECK(f.host.Poll(4000)==di::Result::Audit);
        auto refusal=r.Pop(r.hostKey);CHECK(refusal.payload.find("audit_unavailable")!=std::string::npos&&refusal.payload.find("\"data\":null")!=std::string::npos);
    }
    for(auto count:calls)CHECK(count==0);
    CHECK(!sco::ServiceTableHandedOut(&owner));sco::Release(&owner);
}
int main(int argc,char** argv) {
    CHECK(argc==3);sco::SetGameThread();root=PlainFixturePath(std::filesystem::canonical(argv[2]));
    auto credentialRoot=std::filesystem::canonical(argv[1]);
    const auto relative=credentialRoot.lexically_relative(std::filesystem::path(root));
    // In-tree CMake output is excluded by the host credential policy. Use an
    // independent private fixture directory; do not weaken that policy.
    if(relative.empty()||*relative.begin()!="..")
        credentialRoot=std::filesystem::path(root).parent_path();
    // Sibling fallback stays outside the excluded source root on the same volume.
    const auto outside=credentialRoot.lexically_relative(std::filesystem::path(root));
    CHECK(!outside.empty() && *outside.begin()=="..");
    di::Id nonce{};CHECK(os::Random(nonce.data(),nonce.size()));
    const auto dir=credentialRoot/("runtime-diagnostics-ipc-"+di::Hex(nonce.data(),nonce.size()));
    CreatePrivateFixtureDirectory(dir);
    // The build output is a trusted fixture location, but may be spelled through
    // an alias (notably /tmp on macOS). Resolve only that directory: the transport
    // must continue refusing symlink components and credential-file aliases.
    path=PlainFixturePath(std::filesystem::canonical(dir)/"session.json");
    Vectors();RunDiagnosticsJsonTests();DisabledAndCredential();GeometryAttacks();HostileFrames();PeerChecks();ReadsAndNoSideEffects();
    CHECK(!std::filesystem::exists(path));std::filesystem::remove(dir);std::cout<<"runtime diagnostics IPC: literals, real transport, mutual auth, hostile peers and zero side effects passed\n";
}
