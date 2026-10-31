// SPDX-License-Identifier: GPL-3.0-only
#include "state.h"
#include "fleet_transaction.h"
#ifdef _WIN32
#include "sco/runtime.h"
#endif
#include <cstring>
#include <cctype>
namespace sco::game::asop_host {
Catalog catalog;
Config settings;
std::atomic<bool> admitted{false};
namespace { std::mutex lock; Status status; }
Result Catalog::Configure(const Config& c) {
    if (c.size != sizeof(Config) || c.shipCount > kMaxShips ||
        (c.catalog != CatalogMode::Game && c.catalog != CatalogMode::Ships) ||
        (c.shipCount && !c.shipClasses)) return Result::InvalidConfig;
    Catalog next;
    for (std::size_t i=0; i<c.shipCount; ++i) {
        const char* name = c.shipClasses[i];
        if (!name) return Result::InvalidConfig;
        std::size_t n=0;
        while (n<64 && name[n]) {
            const unsigned char ch = static_cast<unsigned char>(name[n]);
            if (!((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') || (ch>='0' && ch<='9')) && ch!='_' && ch!='-' && ch!='.') return Result::InvalidConfig;
            ++n;
        }
        if (!n || n==64) return Result::InvalidConfig;
        for (std::size_t j=0; j<i; ++j)
            if (FleetNameEqual(next.names[j].data(),name)) return Result::InvalidConfig;
        std::memcpy(next.names[i].data(),name,n+1);
    }
    next.count=c.shipCount;
    *this=next;
    return Result::Ok;
}
Result Start(const Config& c) {
    std::lock_guard guard(lock);
    if (status.configured || status.stopped) return Result::AlreadyStarted;
    Catalog next;
    const auto validation=next.Configure(c);
    if(validation!=Result::Ok) return validation;
    if(!c.enabled) return Result::Disabled;
    if(!c.offlinePrepared || !c.eacAbsent) return Result::AdmissionRefused;
    // Offline admission and signature readiness do not prove a safe phase for
    // patching instructions or the later live slot swap. Do not publish admission
    // or install hooks until a real host/engine lifecycle protocol exists.
    return Result::Unavailable;
}
Result Reconfigure(const Config& c) {
    (void)c;
    // No engine lifecycle contract establishes safe mid-session rebuilding or
    // rebuild/Stop overlap. Refuse before engine calls or host publication.
    return Result::Unavailable;
}
Status Snapshot() {
    std::lock_guard guard(lock); Status copy=status;
#ifdef _WIN32
    copy.ready=FleetReady() | HangarReady();
#endif
    return copy;
}
void Pump(uint32_t now) {
#ifdef _WIN32
    if(admitted.load() && sco::OnGameThread()) {
        PumpInitialFleet();
        TickHangars(now);
        static uint32_t last=0;
        if(now-last>=100) { last=now; TickFleet(now); }
    }
#else
    (void)now;
#endif
}
void Stop() {
    bool wasAdmitted = false;
    {
        std::lock_guard guard(lock);
        wasAdmitted = admitted.load();
#ifdef _WIN32
        if (wasAdmitted) DisableFleetAdmission();
        else admitted.store(false);
#else
        admitted.store(false);
#endif
        status.admitted=false; status.stopped=true;
    }
#ifdef _WIN32
    if (wasAdmitted) {
        CloseHangars();
        CloseFleet();
    }
#else
    (void)wasAdmitted;
#endif
}
} // namespace sco::game::asop_host
