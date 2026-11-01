// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "sco/game/host.h"
#include "sco/game/asop_host.h"
#include "sco/game/pak.h"
#include "sco/game/services.h"
#include "sco/hook.h"
#include "../src/game/datacore_live_lifecycle.h"
#include "sco/reserved_session.h"
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <string>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
namespace rs = sco::reserved;
static sco::Result ReserveEmpty(rs::CoreBinding& binding, rs::Session& session) {
    auto seed = std::make_unique<rs::MockGraph>();
    return rs::ReserveSession(binding, *seed, session);
}
namespace gh = sco::game::host;
static std::promise<void> entered;
static std::shared_future<void> released;
static bool called = false;
static unsigned mountCalls = 0;
static std::shared_ptr<const sco::vfs::Table> Mounts() { ++mountCalls; return {}; }
static gh::Admission Inspect() {
    called = true; entered.set_value(); released.wait();
    return {}; // fail before any native work
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    std::promise<void> release; released = release.get_future().share();
    rs::CoreBinding binding; rs::Session session;
    CHECK(rs::BindCoreInstance(rs::CreateExecutableAuthority(), binding) == sco::Result::Ok);
    if (std::string(argv[1]) == "pak-disable-first") {
        sco::game::pak::Disable();
        CHECK(ReserveEmpty(binding, session) == sco::Result::Unavailable && !session);
        return 0;
    }
    if (std::string(argv[1]) == "switch-reserved") {
        CHECK(ReserveEmpty(binding, session) == sco::Result::Ok);
        const bool relocation = sco::game::services::RelocationEnabled();
        sco::game::services::SetRelocationEnabled(!relocation);
        CHECK(sco::game::services::RelocationEnabled() == relocation);
#ifdef SCO_GAME_SERVICES
        const bool writes = sco::game::services::ActorStatWritesEnabled();
        sco::game::services::SetActorStatWritesEnabled(!writes);
        CHECK(sco::game::services::ActorStatWritesEnabled() == writes);
#endif
        return 0;
    }
    if (std::string(argv[1]) == "switch-legacy") {
        sco::game::services::SetRelocationEnabled(false);
        CHECK(ReserveEmpty(binding, session) == sco::Result::Unavailable && !session);
        return 0;
    }
    if (std::string(argv[1]) == "pak-reserved" || std::string(argv[1]) == "pak-unreserved") {
        if (std::string(argv[1]) == "pak-reserved") CHECK(ReserveEmpty(binding, session) == sco::Result::Ok);
        unsigned char code[] = {0x11, 0x22, 0x33, 0x44};
        uintptr_t cryPak = 0x1234;
        sco::game::pak::Options options; options.mounts = Mounts;
        sco::game::pak::Targets targets{code, &cryPak};
        CHECK(sco::game::pak::Enable(options) == sco::Result::Unavailable);
        CHECK(sco::game::pak::Enable(options, targets) == sco::Result::Unavailable);
        CHECK(!sco::game::pak::Enabled() && sco::hook::DetourCount() == 0 && mountCalls == 0);
        CHECK(code[0] == 0x11 && code[1] == 0x22 && code[2] == 0x33 && code[3] == 0x44 && cryPak == 0x1234);
        if (std::string(argv[1]) == "pak-reserved") {
            sco::game::datacore_live::LifecycleGate::ReadLease reader;
            CHECK(sco::game::datacore_live::ProcessLifecycleGate().EnterRead(reader));
            auto disable = std::async(std::launch::async, [] { sco::game::pak::Disable(); });
            CHECK(disable.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
            disable.get();
        }
        return 0;
    }
    if (std::string(argv[1]) == "asop-reserved") {
        CHECK(ReserveEmpty(binding, session) == sco::Result::Ok);
        CHECK(!sco::game::asop_host::Snapshot().stopped);
        sco::game::asop_host::Stop();
        CHECK(!sco::game::asop_host::Snapshot().stopped);
        return 0;
    }
    if (std::string(argv[1]) == "asop-legacy") {
        sco::game::asop_host::Stop();
        CHECK(sco::game::asop_host::Snapshot().stopped);
        CHECK(ReserveEmpty(binding, session) == sco::Result::Unavailable && !session);
        return 0;
    }
    gh::Options options; options.inspect = Inspect;
    if (std::string(argv[1]) == "reserved-first") {
        CHECK(ReserveEmpty(binding, session) == sco::Result::Ok);
        CHECK(gh::Prepare(options) == sco::Result::Unavailable);
        CHECK(!called && gh::Status().phase == gh::Phase::Idle);
        return 0;
    }
    CHECK(std::string(argv[1]) == "legacy-first");
    auto prepare = std::async(std::launch::async, [&] { return gh::Prepare(options); });
    CHECK(entered.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    CHECK(ReserveEmpty(binding, session) == sco::Result::Unavailable && !session);
    release.set_value();
    CHECK(prepare.get() == sco::Result::Unavailable);
    CHECK(ReserveEmpty(binding, session) == sco::Result::Unavailable && !session);
}
