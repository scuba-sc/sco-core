// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Host-private diagnostics gate. Not an SDK service; default-off product policy.
#include "sco/diagnostics_policy.h"
#include "diagnostics_audit.h"
#include "diagnostics_platform.h"
#include "diagnostics_json.h"
#include "sc_ipc.h"
#include <array>
#include <memory>
#include <vector>
namespace sco::diagnostics::ipc {
enum class Result { Ok, Disabled, Unavailable, Busy, Corrupt, Auth, Epoch, Deadline, Empty, Audit, Rate };
using Id = std::array<uint8_t,16>;
using Key = std::array<uint8_t,32>;
struct Credentials { Id session{}; Key secret{}; uint64_t epoch=0; std::optional<std::string> build; };
struct Frame {
    uint16_t opcode=0; Id session{}, request{}; uint64_t epoch=0, sequence=0, deadline=0;
    uint8_t direction=0; std::string payload;
};
// Private native peer helpers; independent literal vectors test their exact encoding.
std::vector<uint8_t> Encode(const Frame&,const Key&);
Result Decode(const uint8_t*,size_t,const Key&,Frame&,uint32_t limit);
void Derive(const Credentials&,const Key& clientNonce,const Key& hostNonce,Key& peer,Key& host);
bool ParseCredential(std::string_view,Credentials&);
std::string Credential(const Credentials&);
std::string Hex(const uint8_t*,size_t);
bool Unhex(std::string_view,uint8_t*,size_t);
Result Geometry(platform::Mapping&,const Credentials&,sc_ipc_chan&,sc_ipc_ring_view&,sc_ipc_ring_view&,uint64_t);
struct AuditEvent { uint16_t opcode=0; Id request{}; uint64_t time=0; uint32_t bytes=0; const char* outcome="refused"; };
// Fixture acknowledgement only, deliberately no ordinary-file audit fallback.
struct FixtureAudit { virtual ~FixtureAudit()=default; virtual bool Acknowledge(const AuditEvent&)=0; };
struct Options {
    bool enabled=false;
    std::string credentialPath;
    // Trusted startup roots; containment refused in addition to opened-handle checks.
    std::vector<std::string> excludedRoots;
    std::optional<std::string> hostBuild;
    InspectionOptions policy;
};
uint64_t ClockNow(); // transport and collector share one machine monotonic clock
class Host {
public:
    Host(SnapshotStore&,Capture&); ~Host();
    Host(const Host&)=delete; Host& operator=(const Host&)=delete;
    Result Start(const Options&,FixtureAudit*,uint64_t now);
    Result Start(const Options&,uint64_t now); // protected production collector only
    Result Poll(uint64_t now); // one worker only; <=8 queued/control work, no game callbacks
    void Stop(); bool Started() const;
private: struct State; std::unique_ptr<State> state_;
};
// Native open-only asynchronous peer; no socket/process/discovery/auto-retry.
class Peer {
public:
    Peer(); ~Peer(); Peer(const Peer&)=delete; Peer& operator=(const Peer&)=delete;
    Result Begin(const std::string& credentialPath,uint64_t now);
    Result Challenge(uint64_t now); Result Ready(uint64_t now);
    Result Send(uint16_t opcode,std::string_view input,uint64_t now);
    Result Receive(uint64_t now,std::string& output);
    Result Heartbeat(uint64_t now); // idle peer only; no request or observation
    void Close();
private: struct State; std::unique_ptr<State> state_;
};
} // namespace sco::diagnostics::ipc
