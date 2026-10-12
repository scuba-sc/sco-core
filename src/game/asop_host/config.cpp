// SPDX-License-Identifier: GPL-3.0-only
#include "state.h"
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
            if (!std::isalnum(ch) && ch!='_' && ch!='-' && ch!='.') return Result::InvalidConfig;
            ++n;
        }
        if (!n || n==64) return Result::InvalidConfig;
        for (std::size_t j=0; j<i; ++j)
            if (!std::strcmp(next.names[j].data(),name)) return Result::InvalidConfig;
        std::memcpy(next.names[i].data(),name,n+1);
    }
    next.count=c.shipCount;
    *this=next;
    return Result::Ok;
}
Result Start(const Config& c) {
    std::lock_guard guard(lock);
    if (status.configured) return Result::AlreadyStarted;
    Catalog next;
    const auto validation=next.Configure(c);
    if(validation!=Result::Ok) return validation;
    if(!c.enabled) return Result::Disabled;
    if(!c.offlinePrepared || !c.eacAbsent) return Result::AdmissionRefused;
#ifndef _WIN32
    return Result::Unavailable;
#else
    settings=c; settings.shipClasses=nullptr; settings.shipCount=0;
    catalog=next;
    status.configured=true; status.catalogCount=catalog.count;
    admitted.store(true); status.admitted=true;
    status.installed=InstallCatalog();
    return status.installed ? Result::Ok : Result::Unavailable;
#endif
}
Status Snapshot() { std::lock_guard guard(lock); return status; }
void Pump(uint32_t now) { (void)now; }
void Stop() {
    std::lock_guard guard(lock);
    admitted.store(false); status.admitted=false; status.stopped=true;
}
} // namespace sco::game::asop_host
