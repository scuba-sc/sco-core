// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "sco/reserved_session.h"
#include "../src/reserved_legacy_gate.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#if defined(__linux__)
#include <unistd.h>
#endif

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
namespace rs = sco::reserved;
static sco::Result ReserveEmpty(rs::CoreBinding& binding, rs::Session& session) {
    auto seed = std::make_unique<rs::MockGraph>();
    return rs::ReserveSession(binding, *seed, session);
}
using namespace std::chrono_literals;

struct Gate {
    std::promise<void> entered;
    std::shared_future<void> release;
    bool fail = false;
    std::promise<void> left;
    static void Hold(void* p) {
        auto& g = *static_cast<Gate*>(p);
        g.entered.set_value();
        g.release.wait();
        g.left.set_value();
        if (g.fail) throw std::runtime_error("injected copy failure");
    }
};
static void HoldNoexcept(void* context) noexcept {
    try { Gate::Hold(context); } catch (...) { std::abort(); }
}

static const rs::Bootstrap* Setup(rs::CoreBinding& binding, rs::Session& session) {
    const auto* b = rs::CreateExecutableAuthority();
    CHECK(b && b->size == sizeof(*b) && b->version == rs::kBootstrapVersion);
    CHECK(rs::BindCoreInstance(b, binding) == sco::Result::Ok);
    CHECK(ReserveEmpty(binding, session) == sco::Result::Ok);
    CHECK(rs::Status(session) == rs::Phase::Reserved);
    rs::StartupControl control;
    CHECK(rs::BeginStartup(session, control) == sco::Result::Ok);
    CHECK(rs::PublishTypedProvider(control) == sco::Result::Ok);
    CHECK(rs::FinishStartup(control) == sco::Result::Ok);
    CHECK(rs::Status(session) == rs::Phase::Running);
    return b;
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string mode = argv[1];
#if defined(__linux__)
    if (mode == "cwd") {
        const auto* b = rs::CreateExecutableAuthority(); CHECK(b);
        CHECK(chdir("/") == 0);
        CHECK(rs::CreateExecutableAuthority() == b);
        rs::CoreBinding binding;
        CHECK(rs::BindCoreInstance(b, binding) == sco::Result::Ok);
        return 0;
    }
#endif
    if (mode == "bootstrap") {
        const auto* b = rs::CreateExecutableAuthority();
        CHECK(b);
        auto shortTable = *b; shortTable.size = sizeof(*b) - 1;
        rs::CoreBinding invalid;
        CHECK(rs::BindCoreInstance(&shortTable, invalid) == sco::Result::BadArg && !invalid);
        auto fake = *b; int unrelated = 0; fake.context = &unrelated;
        fake.bind = [](void*, const void*, void** out) noexcept {
            static int bogusBinding;
            if (out) *out = &bogusBinding;
            return sco::Result::Ok;
        };
        CHECK(rs::BindCoreInstance(&fake, invalid) == sco::Result::Unavailable && !invalid);
        rs::CoreBinding binding;
        CHECK(rs::BindCoreInstance(b, binding) == sco::Result::Ok && binding);
        int otherIdentity = 0; void* forged = nullptr;
        CHECK(b->bind(b->context, &otherIdentity, &forged) == sco::Result::Unavailable && !forged);
        void* session = nullptr; void* secret = nullptr;
        auto graph = std::make_unique<rs::MockGraph>();
        CHECK(b->reserve(b->context, b->context, graph.get(), &session, &secret) == sco::Result::Unavailable);
        CHECK(!session && !secret);
        return 0;
    }
    if (mode == "reentry") {
        const auto* b = rs::CreateExecutableAuthority(); CHECK(b);
        static int identity; void* binding = nullptr;
        CHECK(b->bind(b->context, &identity, &binding) == sco::Result::Ok && binding);
        void* session = nullptr; void* secret = nullptr;
        auto graph = std::make_unique<rs::MockGraph>();
        CHECK(b->reserve(b->context, binding, graph.get(), &session, &secret) == sco::Result::Ok);
        CHECK(session && secret && session != secret);
        int callbackControl = 0, ownerControl = 0;
        CHECK(b->begin(b->context, session, session, &callbackControl) == sco::Result::Unavailable);
        CHECK(b->begin(b->context, session, secret, &ownerControl) == sco::Result::Ok);
        CHECK(b->publish_mock(b->context, session, secret, &callbackControl) == sco::Result::Unavailable);
        CHECK(b->finish(b->context, session, secret, &callbackControl) == sco::Result::Unavailable);
        std::thread wrongThread([&] {
            CHECK(b->publish_mock(b->context, session, secret, &ownerControl) == sco::Result::Unavailable);
            CHECK(b->finish(b->context, session, secret, &ownerControl) == sco::Result::Unavailable);
        });
        wrongThread.join();
        CHECK(b->publish_mock(b->context, session, session, &callbackControl) == sco::Result::Unavailable);
        CHECK(b->finish(b->context, session, session, &callbackControl) == sco::Result::Unavailable);
        CHECK(!b->table(b->context, session, secret));
        CHECK(b->publish_mock(b->context, session, secret, &ownerControl) == sco::Result::Ok);
        CHECK(b->finish(b->context, session, secret, &ownerControl) == sco::Result::Ok);
        CHECK(b->table(b->context, session, secret));
        return 0;
    }
    if (mode == "legacy-active" || mode == "legacy-dirty") {
        rs::CoreBinding binding; rs::Session session;
        CHECK(rs::BindCoreInstance(rs::CreateExecutableAuthority(), binding) == sco::Result::Ok);
        {
            sco::detail::LegacyEntry entry(mode == "legacy-dirty");
            CHECK(static_cast<bool>(entry));
            CHECK(ReserveEmpty(binding, session) == sco::Result::Unavailable && !session);
        }
        if (mode == "legacy-dirty") {
            CHECK(ReserveEmpty(binding, session) == sco::Result::Unavailable && !session);
            return 0;
        }
        CHECK(ReserveEmpty(binding, session) == sco::Result::Ok && session);
        sco::detail::LegacyEntry observation(false), mutation;
        CHECK(!observation && !mutation);
        return 0;
    }
    if (mode == "allocation" || mode == "startup-fault" || mode == "startup-close") {
        rs::CoreBinding binding; rs::Session session; rs::StartupControl control;
        const auto* b = rs::CreateExecutableAuthority(); CHECK(b);
        CHECK(rs::BindCoreInstance(b, binding) == sco::Result::Ok);
        if (mode == "allocation") {
            rs::testing::FailNextReservationAllocation();
            CHECK(ReserveEmpty(binding, session) == sco::Result::TooMany && !session);
            CHECK(ReserveEmpty(binding, session) == sco::Result::Ok && session);
            CHECK(rs::Status(session) == rs::Phase::Reserved);
            return 0;
        }
        CHECK(ReserveEmpty(binding, session) == sco::Result::Ok);
        CHECK(rs::BeginStartup(session, control) == sco::Result::Ok);
        CHECK(rs::PublishTypedProvider(control) == sco::Result::Ok);
        if (mode == "startup-fault") rs::Fault(session);
        else CHECK(rs::Close(session) == sco::Result::Failed);
        CHECK(rs::FinishStartup(control) == sco::Result::Unavailable);
        CHECK(rs::Status(session) == rs::Phase::FailedRetained);
        rs::Session second;
        CHECK(ReserveEmpty(binding, second) == sco::Result::Unavailable && !second);
        return 0;
    }
    if (mode == "worker-wrapper-destruction") {
        rs::CoreBinding binding;
        auto session = std::make_unique<rs::Session>();
        Setup(binding, *session);
        const auto table = *rs::QueryMock(*session);
        std::promise<void> release;
        auto* gate = new Gate{{}, release.get_future().share(), false, {}};
        rs::testing::SetBeforeQueuedWorkHook(Gate::Hold, gate);
        CHECK(table.start_worker(table.context) == sco::Result::Ok);
        CHECK(table.enqueue_set(table.context, 7) == sco::Result::Ok);
        CHECK(gate->entered.get_future().wait_for(2s) == std::future_status::ready);
        session.reset(); // faults, but the owning capsule and worker remain rooted
        uint64_t unchanged = 999;
        CHECK(table.queued_value(table.context, &unchanged) == sco::Result::Unavailable && unchanged == 999);
        release.set_value(); gate->left.get_future().wait();
        CHECK(table.enqueue_add(table.context, 1) == sco::Result::Unavailable);
        rs::testing::SetBeforeQueuedWorkHook(nullptr, nullptr);
        return 0;
    }
    if (mode == "graph-concurrent") {
        rs::CoreBinding binding; rs::Session session; rs::StartupControl control;
        CHECK(rs::BindCoreInstance(rs::CreateExecutableAuthority(), binding) == sco::Result::Ok);
        auto seed = std::make_unique<rs::MockGraph>();
        CHECK(rs::ReserveSession(binding, *seed, session) == sco::Result::Ok);
        CHECK(rs::BeginStartup(session, control) == sco::Result::Ok);
        CHECK(rs::PublishTypedProvider(control) == sco::Result::Ok);
        CHECK(rs::FinishStartup(control) == sco::Result::Ok);
        const auto table = *rs::QueryMock(session);
        std::thread writers[4];
        for (size_t t = 0; t < 4; ++t) writers[t] = std::thread([&, t] {
            for (size_t i = 0; i < 64; ++i) {
                rs::MockEntity entity{}; entity.id = 1 + t * 64 + i;
                std::strcpy(entity.name, "entity");
                entity.position[0] = static_cast<double>(i);
                CHECK(table.put_entity(table.context, &entity) == sco::Result::Ok);
                rs::MockEntity copy{};
                CHECK(table.get_entity(table.context, entity.id, &copy) == sco::Result::Ok && copy.position[0] == entity.position[0]);
            }
        });
        for (auto& writer : writers) writer.join();
        uint32_t count = 0; CHECK(table.entity_count(table.context, &count) == sco::Result::Ok && count == 256);
        CHECK(rs::Close(session) == sco::Result::Ok);
        CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + 3s) == rs::RetireResult::QuiescedRetained);
        rs::Fault(session);
        CHECK(rs::Status(session) == rs::Phase::QuiescedRetained);
        return 0;
    }
    if (mode == "graph-boundary") {
        rs::CoreBinding binding; rs::Session session; rs::StartupControl control;
        CHECK(rs::BindCoreInstance(rs::CreateExecutableAuthority(), binding) == sco::Result::Ok);
        auto seed = std::make_unique<rs::MockGraph>();
        seed->zoneCount = 1; seed->zones[0].id = 1; std::strcpy(seed->zones[0].name, "zone");
        seed->size = 0;
        CHECK(rs::ReserveSession(binding, *seed, session) == sco::Result::BadArg && !session);
        seed->size = sizeof(*seed); seed->version = 99;
        CHECK(rs::ReserveSession(binding, *seed, session) == sco::Result::BadArg && !session);
        seed->version = rs::kMockGraphVersion; seed->zoneCount = rs::kMockMaxZones + 1;
        CHECK(rs::ReserveSession(binding, *seed, session) == sco::Result::BadArg && !session);
        seed->zoneCount = 1; seed->zones[0].parent = 99;
        CHECK(rs::ReserveSession(binding, *seed, session) == sco::Result::BadArg && !session);
        seed->zones[0].parent = 0; seed->entityCount = 2;
        seed->entities[0].id = 42; seed->entities[0].zone = 1; std::strcpy(seed->entities[0].name, "entity");
        seed->entities[1] = seed->entities[0];
        CHECK(rs::ReserveSession(binding, *seed, session) == sco::Result::BadArg && !session);
        seed->entities[1].id = 43; seed->entities[1].name[0] = '\0';
        CHECK(rs::ReserveSession(binding, *seed, session) == sco::Result::BadArg && !session);
        std::memset(seed->entities[1].name, 'x', sizeof(seed->entities[1].name));
        CHECK(rs::ReserveSession(binding, *seed, session) == sco::Result::BadArg && !session);
        std::strcpy(seed->entities[1].name, "entity2");
        seed->entities[1].position[0] = std::numeric_limits<double>::quiet_NaN();
        CHECK(rs::ReserveSession(binding, *seed, session) == sco::Result::BadArg && !session);
        seed->entities[1].position[0] = 0;
        CHECK(rs::ReserveSession(binding, *seed, session) == sco::Result::Ok);
        CHECK(rs::BeginStartup(session, control) == sco::Result::Ok);
        CHECK(rs::PublishTypedProvider(control) == sco::Result::Ok);
        CHECK(rs::FinishStartup(control) == sco::Result::Ok);
        const auto table = *rs::QueryMock(session);
        for (size_t i = 2; i < rs::kMockMaxEntities; ++i) {
            rs::MockEntity entity{}; entity.id = i + 100; entity.zone = 1; std::strcpy(entity.name, "entity");
            CHECK(table.put_entity(table.context, &entity) == sco::Result::Ok);
        }
        rs::MockEntity extra{}; extra.id = 999999; extra.zone = 1; std::strcpy(extra.name, "extra");
        CHECK(table.put_entity(table.context, &extra) == sco::Result::TooMany);
        CHECK(table.remove_entity(table.context, 42) == sco::Result::Ok);
        CHECK(table.put_entity(table.context, &extra) == sco::Result::Ok);
        uint32_t count = 0; CHECK(table.entity_count(table.context, &count) == sco::Result::Ok && count == rs::kMockMaxEntities);
        CHECK(rs::Close(session) == sco::Result::Ok);
        CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + 2s) == rs::RetireResult::QuiescedRetained);
        return 0;
    }
    if (mode == "graph" || mode == "graph-held-copy") {
        rs::CoreBinding binding; rs::Session session; rs::StartupControl control;
        CHECK(rs::BindCoreInstance(rs::CreateExecutableAuthority(), binding) == sco::Result::Ok);
        auto seed = std::make_unique<rs::MockGraph>();
        seed->zoneCount = 2;
        seed->zones[0].id = 1; std::strcpy(seed->zones[0].name, "system");
        seed->zones[1].id = 2; seed->zones[1].parent = 1; std::strcpy(seed->zones[1].name, "station");
        seed->entityCount = 1; seed->entities[0].id = 100; seed->entities[0].zone = 2;
        seed->entities[0].position[0] = 3; std::strcpy(seed->entities[0].name, "MockPlayer");
        seed->zones[0].parent = 2;
        CHECK(rs::ReserveSession(binding, *seed, session) == sco::Result::BadArg && !session);
        seed->zones[0].parent = 0;
        seed->entities[0].position[0] = std::numeric_limits<double>::infinity();
        CHECK(rs::ReserveSession(binding, *seed, session) == sco::Result::BadArg && !session);
        seed->entities[0].position[0] = 3;
        CHECK(rs::ReserveSession(binding, *seed, session) == sco::Result::Ok);
        seed->entities[0].position[0] = 900; // no alias to retained graph
        CHECK(rs::BeginStartup(session, control) == sco::Result::Ok);
        CHECK(rs::PublishTypedProvider(control) == sco::Result::Ok);
        CHECK(rs::FinishStartup(control) == sco::Result::Ok);
        const auto* table = rs::QueryMock(session); CHECK(table);
        const auto cached = *table;
        uint32_t zones = 0; rs::MockZone zone{};
        CHECK(cached.zone_count(cached.context, &zones) == sco::Result::Ok && zones == 2);
        CHECK(cached.zone_at(cached.context, 1, &zone) == sco::Result::Ok && zone.id == 2 && zone.parent == 1);
        CHECK(cached.zone_at(cached.context, 2, &zone) == sco::Result::NotFound && zone.id == 2);
        rs::MockEntity out{};
        CHECK(cached.get_entity(cached.context, 100, &out) == sco::Result::Ok && out.position[0] == 3);
        rs::MockEntity added{}; added.id = 101; added.zone = 2;
        std::strcpy(added.name, "MockProp"); added.position[1] = 9;
        CHECK(cached.put_entity(cached.context, &added) == sco::Result::Ok);
        uint32_t count = 0;
        CHECK(cached.entity_count(cached.context, &count) == sco::Result::Ok && count == 2);
        CHECK(cached.get_entity(cached.context, 101, &out) == sco::Result::Ok && out.position[1] == 9);
        CHECK(cached.entity_at(cached.context, 1, &out) == sco::Result::Ok && out.id == 101);
        CHECK(cached.entity_at(cached.context, 2, &out) == sco::Result::NotFound && out.id == 101);
        added.zone = 999;
        CHECK(cached.put_entity(cached.context, &added) == sco::Result::BadArg);
        CHECK(cached.get_entity(cached.context, 101, &out) == sco::Result::Ok && out.zone == 2);
        if (mode == "graph-held-copy") {
            std::promise<void> release;
            Gate gate{{}, release.get_future().share(), false, {}};
            rs::testing::SetBeforeCopyHook(Gate::Hold, &gate);
            out.id = 0;
            auto call = std::async(std::launch::async, [&] { return cached.get_entity(cached.context, 100, &out); });
            CHECK(gate.entered.get_future().wait_for(2s) == std::future_status::ready);
            CHECK(rs::Close(session) == sco::Result::Ok);
            auto retire = std::async(std::launch::async, [&] { return rs::RetireUntil(session, std::chrono::steady_clock::now() + 3s); });
            CHECK(retire.wait_for(50ms) == std::future_status::timeout);
            release.set_value(); CHECK(call.get() == sco::Result::Ok && out.id == 100);
            CHECK(retire.get() == rs::RetireResult::QuiescedRetained);
            rs::testing::SetBeforeCopyHook(nullptr, nullptr);
        } else {
            CHECK(cached.remove_entity(cached.context, 101) == sco::Result::Ok);
            CHECK(cached.get_entity(cached.context, 101, &out) == sco::Result::NotFound);
            CHECK(rs::Close(session) == sco::Result::Ok);
            CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + 2s) == rs::RetireResult::QuiescedRetained);
        }
        out.id = 999;
        CHECK(cached.get_entity(cached.context, 100, &out) == sco::Result::Unavailable && out.id == 999);
        CHECK(cached.put_entity(cached.context, &added) == sco::Result::Unavailable);
        CHECK(cached.remove_entity(cached.context, 100) == sco::Result::Unavailable);
        CHECK(cached.zone_at(cached.context, 0, &zone) == sco::Result::Unavailable && zone.id == 2);
        return 0;
    }
    rs::CoreBinding binding;
    rs::Session session;
    const auto* b = Setup(binding, session); (void)b;
    const auto* table = rs::QueryMock(session); CHECK(table);
    const auto cached = *table;
    CHECK(cached.set(cached.context, 42) == sco::Result::Ok);
    CHECK(cached.tick(cached.context, 3) == sco::Result::Ok);
    int64_t value = 0; uint64_t ticks = 0;
    CHECK(cached.snapshot(cached.context, &value, &ticks) == sco::Result::Ok && value == 42 && ticks == 3);
    if (mode == "all-slots-closed") {
        CHECK(cached.start_worker(cached.context) == sco::Result::Ok);
        CHECK(rs::Close(session) == sco::Result::Ok);
        CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + 3s) == rs::RetireResult::QuiescedRetained);
        rs::MockEntity entity{}; entity.id = 71; std::strcpy(entity.name, "fixture");
        rs::MockZone zone{}; zone.id = 9;
        int64_t scalar = 77; uint64_t serial = 88; uint32_t count = 99;
        CHECK(cached.set(cached.context, 1) == sco::Result::Unavailable);
        CHECK(cached.get(cached.context, &scalar) == sco::Result::Unavailable && scalar == 77);
        CHECK(cached.tick(cached.context, 1) == sco::Result::Unavailable);
        CHECK(cached.snapshot(cached.context, &scalar, &serial) == sco::Result::Unavailable && scalar == 77 && serial == 88);
        CHECK(cached.put_entity(cached.context, &entity) == sco::Result::Unavailable);
        CHECK(cached.get_entity(cached.context, 71, &entity) == sco::Result::Unavailable && entity.id == 71);
        CHECK(cached.remove_entity(cached.context, 71) == sco::Result::Unavailable);
        CHECK(cached.entity_count(cached.context, &count) == sco::Result::Unavailable && count == 99);
        CHECK(cached.start_worker(cached.context) == sco::Result::Unavailable);
        CHECK(cached.enqueue_set(cached.context, 1) == sco::Result::Unavailable);
        CHECK(cached.enqueue_add(cached.context, 1) == sco::Result::Unavailable);
        CHECK(cached.queued_value(cached.context, &serial) == sco::Result::Unavailable && serial == 88);
        CHECK(cached.zone_count(cached.context, &count) == sco::Result::Unavailable && count == 99);
        CHECK(cached.zone_at(cached.context, 0, &zone) == sco::Result::Unavailable && zone.id == 9);
        CHECK(cached.entity_at(cached.context, 0, &entity) == sco::Result::Unavailable && entity.id == 71);
        CHECK(cached.enqueue_set_tracked(cached.context, 1, &serial) == sco::Result::Unavailable && serial == 88);
        CHECK(cached.enqueue_add_tracked(cached.context, 1, &serial) == sco::Result::Unavailable && serial == 88);
        uint64_t completed = 66;
        CHECK(cached.queued_progress(cached.context, &serial, &completed) == sco::Result::Unavailable && serial == 88 && completed == 66);
        return 0;
    }
    if (mode == "worker-allocation-fault" || mode == "worker-launch-fault") {
        if (mode == "worker-allocation-fault") rs::testing::FailNextWorkerAllocation();
        else rs::testing::FailNextWorkerLaunch();
        CHECK(cached.start_worker(cached.context) == sco::Result::Failed);
        CHECK(rs::Status(session) == rs::Phase::FailedRetained);
        CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + 1s) == rs::RetireResult::FailedRetained);
        CHECK(cached.start_worker(cached.context) == sco::Result::Unavailable);
        return 0;
    }
    if (mode == "worker-ticket-copy" || mode == "worker-ticket-copy-fault") {
        CHECK(cached.start_worker(cached.context) == sco::Result::Ok);
        std::promise<void> release;
        Gate gate{{}, release.get_future().share(), mode == "worker-ticket-copy-fault", {}};
        rs::testing::SetBeforeCopyHook(Gate::Hold, &gate);
        uint64_t ticket = 0;
        auto enqueue = std::async(std::launch::async, [&] { return cached.enqueue_set_tracked(cached.context, 7, &ticket); });
        CHECK(gate.entered.get_future().wait_for(2s) == std::future_status::ready);
        CHECK(rs::Close(session) == sco::Result::Ok);
        auto retire = std::async(std::launch::async, [&] { return rs::RetireUntil(session, std::chrono::steady_clock::now() + 4s); });
        CHECK(retire.wait_for(50ms) == std::future_status::timeout);
        release.set_value();
        if (gate.fail) {
            CHECK(enqueue.get() == sco::Result::Failed && ticket == 0);
            CHECK(retire.get() == rs::RetireResult::FailedRetained);
        } else {
            CHECK(enqueue.get() == sco::Result::Ok && ticket != 0);
            CHECK(retire.get() == rs::RetireResult::QuiescedRetained);
        }
        rs::testing::SetBeforeCopyHook(nullptr, nullptr);
        return 0;
    }
    if (mode == "worker-queue-fault-before-seal") {
        CHECK(cached.start_worker(cached.context) == sco::Result::Ok);
        CHECK(rs::Close(session) == sco::Result::Ok);
        rs::testing::FailQueueBeforeSeal();
        CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + 3s) == rs::RetireResult::FailedRetained);
        CHECK(rs::Status(session) == rs::Phase::FailedRetained);
        return 0;
    }
    if (mode == "worker-value") {
        CHECK(cached.start_worker(cached.context) == sco::Result::Ok);
        auto waitFor = [&](uint64_t ticket, uint64_t expected) {
            const auto end = std::chrono::steady_clock::now() + 3s;
            uint64_t observed = 0, completed = 0;
            while (std::chrono::steady_clock::now() < end) {
                if (cached.queued_progress(cached.context, &observed, &completed) == sco::Result::Ok && completed >= ticket)
                    return observed == expected;
                std::this_thread::yield();
            }
            return false;
        };
        uint64_t first = 0, second = 0, third = 0;
        CHECK(cached.enqueue_set_tracked(cached.context, 0, &first) == sco::Result::Ok && first != 0 && waitFor(first, 0));
        CHECK(cached.enqueue_set_tracked(cached.context, 11, &second) == sco::Result::Ok && second > first && waitFor(second, 11));
        CHECK(cached.enqueue_add_tracked(cached.context, 4, &third) == sco::Result::Ok && third > second && waitFor(third, 15));
        CHECK(cached.queued_progress(cached.context, &first, &first) == sco::Result::BadArg);
        CHECK(rs::Close(session) == sco::Result::Ok);
        CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + 3s) == rs::RetireResult::QuiescedRetained);
        rs::Fault(session);
        CHECK(rs::Status(session) == rs::Phase::QuiescedRetained);
        uint64_t unchanged = 99;
        CHECK(cached.queued_value(cached.context, &unchanged) == sco::Result::Unavailable && unchanged == 99);
        return 0;
    }
    if (mode == "worker-drain" || mode == "worker-fault" || mode == "worker-timeout" ||
        mode == "worker-tls" || mode == "worker-close-race" || mode == "worker-publish-close" || mode == "worker-commit-fault") {
        std::promise<void> release;
        // A timed-out worker may outlive this scope; keep its test hook context
        // process-rooted just like the production capsule.
        auto* gate = new Gate{{}, release.get_future().share(), mode == "worker-fault", {}};
        if (mode == "worker-close-race" || mode == "worker-publish-close") {
            if (mode == "worker-close-race") rs::testing::SetBeforeWorkerStartHook(Gate::Hold, gate);
            else rs::testing::SetBeforeWorkerPublishHook(HoldNoexcept, gate);
            auto start = std::async(std::launch::async, [&] { return cached.start_worker(cached.context); });
            CHECK(gate->entered.get_future().wait_for(2s) == std::future_status::ready);
            CHECK(rs::Close(session) == sco::Result::Ok);
            auto retire = std::async(std::launch::async, [&] { return rs::RetireUntil(session, std::chrono::steady_clock::now() + 4s); });
            CHECK(retire.wait_for(50ms) == std::future_status::timeout);
            release.set_value(); CHECK(start.get() == sco::Result::Unavailable);
            CHECK(retire.get() == rs::RetireResult::QuiescedRetained);
            rs::testing::SetBeforeWorkerStartHook(nullptr, nullptr);
            rs::testing::SetBeforeWorkerPublishHook(nullptr, nullptr);
            return 0;
        }
        if (mode == "worker-tls") rs::testing::SetWorkerTlsExitHook(Gate::Hold, gate);
        if (mode == "worker-commit-fault") rs::testing::SetBeforeTerminalCommitHook(Gate::Hold, gate);
        CHECK(cached.enqueue_set(cached.context, 11) == sco::Result::Unavailable);
        CHECK(cached.start_worker(cached.context) == sco::Result::Ok);
        CHECK(cached.start_worker(cached.context) == sco::Result::Unavailable);
        if (mode == "worker-drain" || mode == "worker-fault" || mode == "worker-timeout") {
            rs::testing::SetBeforeQueuedWorkHook(Gate::Hold, gate);
            CHECK(cached.enqueue_set(cached.context, 11) == sco::Result::Ok);
            CHECK(gate->entered.get_future().wait_for(2s) == std::future_status::ready);
            if (mode == "worker-fault") {
                release.set_value(); gate->left.get_future().wait();
                const auto end = std::chrono::steady_clock::now() + 2s;
                while (rs::Status(session) != rs::Phase::FailedRetained && std::chrono::steady_clock::now() < end) std::this_thread::yield();
                CHECK(rs::Status(session) == rs::Phase::FailedRetained);
                CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + 1s) == rs::RetireResult::FailedRetained);
                rs::testing::SetBeforeQueuedWorkHook(nullptr, nullptr);
                return 0;
            }
        }
        CHECK(rs::Close(session) == sco::Result::Ok);
        CHECK(cached.enqueue_add(cached.context, 4) == sco::Result::Unavailable);
        if (mode == "worker-timeout") {
            CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + 50ms) == rs::RetireResult::FailedRetained);
            release.set_value(); gate->left.get_future().wait();
            CHECK(rs::Status(session) == rs::Phase::FailedRetained);
            CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + 1s) == rs::RetireResult::FailedRetained);
            rs::testing::SetBeforeQueuedWorkHook(nullptr, nullptr);
            return 0;
        }
        auto retire = std::async(std::launch::async, [&] { return rs::RetireUntil(session, std::chrono::steady_clock::now() + 4s); });
        if (mode == "worker-tls" || mode == "worker-commit-fault")
            CHECK(gate->entered.get_future().wait_for(2s) == std::future_status::ready);
        CHECK(retire.wait_for(50ms) == std::future_status::timeout);
        if (mode == "worker-commit-fault") rs::Fault(session);
        release.set_value();
        CHECK(retire.get() == (mode == "worker-commit-fault" ? rs::RetireResult::FailedRetained : rs::RetireResult::QuiescedRetained));
        rs::testing::SetBeforeQueuedWorkHook(nullptr, nullptr);
        rs::testing::SetWorkerTlsExitHook(nullptr, nullptr);
        rs::testing::SetBeforeTerminalCommitHook(nullptr, nullptr);
        return 0;
    }
    if (mode == "held-copy" || mode == "fault-copy") {
        std::promise<void> release;
        Gate gate{{}, release.get_future().share(), mode == "fault-copy", {}};
        rs::testing::SetBeforeCopyHook(Gate::Hold, &gate);
        value = -1; ticks = 99;
        auto call = std::async(std::launch::async, [&] { return cached.snapshot(cached.context, &value, &ticks); });
        CHECK(gate.entered.get_future().wait_for(2s) == std::future_status::ready);
        CHECK(rs::Close(session) == sco::Result::Ok);
        auto retiring = std::async(std::launch::async, [&] { return rs::RetireUntil(session, std::chrono::steady_clock::now() + 3s); });
        CHECK(retiring.wait_for(50ms) == std::future_status::timeout);
        release.set_value();
        const auto called = call.get();
        const auto retired = retiring.get();
        if (gate.fail) {
            CHECK(called == sco::Result::Failed && value == -1 && ticks == 99);
            CHECK(retired == rs::RetireResult::FailedRetained && rs::Status(session) == rs::Phase::FailedRetained);
        } else {
            CHECK(called == sco::Result::Ok && value == 42 && ticks == 3);
            CHECK(retired == rs::RetireResult::QuiescedRetained && rs::Status(session) == rs::Phase::QuiescedRetained);
            rs::Fault(session);
            CHECK(rs::Status(session) == rs::Phase::QuiescedRetained);
        }
        rs::testing::SetBeforeCopyHook(nullptr, nullptr);
        CHECK(cached.get(cached.context, nullptr) == sco::Result::Unavailable);
        CHECK(cached.snapshot(cached.context, nullptr, nullptr) == sco::Result::Unavailable);
        return 0;
    }
    if (mode == "timeout") {
        std::promise<void> entered, release; const auto released = release.get_future().share();
        std::thread caller([&] { rs::CallPin pin; CHECK(rs::TryEnter(session, pin) == sco::Result::Ok); entered.set_value(); released.wait(); });
        entered.get_future().wait();
        CHECK(rs::Close(session) == sco::Result::Ok);
        CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + 50ms) == rs::RetireResult::FailedRetained);
        release.set_value(); caller.join();
        CHECK(rs::Status(session) == rs::Phase::FailedRetained);
        CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + 1s) == rs::RetireResult::FailedRetained);
        CHECK(cached.get(cached.context, nullptr) == sco::Result::Unavailable);
        return 0;
    }
    if (mode == "self") {
        rs::CallPin pin;
        CHECK(rs::TryEnter(session, pin) == sco::Result::Ok);
        CHECK(rs::Close(session) == sco::Result::Ok);
        CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + 1s) == rs::RetireResult::FailedRetained);
        return 0;
    }
    if (mode == "two-retirers") {
        std::promise<void> entered, release; const auto released = release.get_future().share();
        std::thread caller([&] { rs::CallPin pin; CHECK(rs::TryEnter(session, pin) == sco::Result::Ok); entered.set_value(); released.wait(); });
        entered.get_future().wait(); CHECK(rs::Close(session) == sco::Result::Ok);
        auto retire = [&] { return rs::RetireUntil(session, std::chrono::steady_clock::now() + 10s); };
        auto first = std::async(std::launch::async, retire), second = std::async(std::launch::async, retire);
        // One attempt must refuse without waiting for the held reader.
        const auto firstReady = first.wait_for(2s) == std::future_status::ready;
        const auto secondReady = second.wait_for(2s) == std::future_status::ready;
        CHECK(firstReady != secondReady);
        CHECK((firstReady ? first.get() : second.get()) == rs::RetireResult::Refused);
        release.set_value(); caller.join();
        CHECK((firstReady ? second.get() : first.get()) == rs::RetireResult::QuiescedRetained);
        CHECK(rs::Status(session) == rs::Phase::QuiescedRetained);
        return 0;
    }
    std::fprintf(stderr, "unknown mode\n"); return 2;
}
