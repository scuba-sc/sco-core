// SPDX-License-Identifier: GPL-3.0-only
#include <sc_container_contents.h>
#include <scosdk/game/container_contents.hpp>
#include "../src/game/services/container_contents.h"
#include "sco/host.h"
#include "sco/caps.h"
#include "../src/api/internal.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
namespace G = sco::game::services;
static int pass = 0, fail = 0;
#define CHECK(x) do { if (x) ++pass; else { ++fail; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
struct Provider { int mode = 0; unsigned calls = 0; };
static const sc_container_contents_v1* recursive_table = nullptr;
static sco_plugin* recursive_self = nullptr;
static sco_result recursive_result = SCO_OK;
static sco_result Read(void* ctx, uint64_t id, sc_container_snapshot* out) {
    auto& p = *static_cast<Provider*>(ctx); ++p.calls;
    if (p.mode == 1) return SCO_NOT_FOUND;
    if (p.mode == 2) throw std::runtime_error("fixture fault");
    if (p.mode == 10) {
        sc_container_snapshot nested{}; nested.size = sizeof(nested); nested.count = 64;
        recursive_result = sc_container_contents_read(recursive_table, recursive_self, id, &nested);
    }
    out->container_id = id; out->generation = 7;
    out->count = p.mode == 3 ? 0 : 2;
    out->total_seen = out->count;
    out->flags = SC_CONTAINER_CONTENTS_VOLUME_KNOWN;
    out->used_volume = out->count ? 16 : 0;
    out->items[0] = {sizeof(sc_container_item), 2, 101, 901, 3};
    out->items[1] = {sizeof(sc_container_item), 1, 102, 902, 10};
    if (p.mode == 4) out->items[0].quantity = 0;
    if (p.mode == 5) out->items[1].item_id = 101;
    if (p.mode == 6) out->items[0].unit_volume = UINT64_MAX;
    if (p.mode == 7) out->used_volume = 15;
    if (p.mode == 8) out->flags = 16;
    if (p.mode == 9) { out->total_seen = 5; out->flags |= SC_CONTAINER_CONTENTS_TRUNCATED; out->used_volume = 50; }
    if (p.mode == 11) out->generation = 0;
    if (p.mode == 12) out->container_id = id + 1;
    if (p.mode == 13) { out->flags = 0; out->used_volume = 0; }
    if (p.mode == 14) out->count = SC_CONTAINER_CONTENTS_MAX_ITEMS + 1;
    if (p.mode == 15) out->total_seen = 1;
    if (p.mode == 16) out->items[0].class_id = 0;
    if (p.mode == 17) sco::caps::Set("game.inventory.contents.read", false, "provider revoke");
    return SCO_OK;
}
static sc_container_snapshot Output(uint32_t capacity = 64) {
    sc_container_snapshot out{}; out.size = sizeof(out); out.count = capacity; return out;
}
int main() {
    sco::SetGameThread();
    auto* self = sco::host::NewPlugin("container_test");
    const auto* api = sco::host::BuildApi({"container fixture"});
    const void* raw = nullptr;
    CHECK(api->query_service(SC_CONTAINER_CONTENTS_NAME, SC_CONTAINER_CONTENTS_VERSION_1_0, &raw) == SCO_NOT_FOUND);
    CHECK(G::StartContainerContents() == sco::Result::Ok);
    CHECK(api->query_service(SC_CONTAINER_CONTENTS_NAME, SC_CONTAINER_CONTENTS_VERSION_1_0, &raw) == SCO_OK);
    auto* table = static_cast<const sc_container_contents_v1*>(raw);
    auto out = Output();
    CHECK(sc_container_contents_read(table, self, 42, &out) == SCO_UNAVAILABLE && out.count == 0);
    G::StopContainerContents();
    Provider provider;
    CHECK(G::StartContainerContents({&provider, Read}) == sco::Result::Ok);
    CHECK(G::ConfigureProductionContainerContents({&provider, Read}) == sco::Result::Failed);
    CHECK(G::StartContainerContents({&provider, Read}) == sco::Result::Failed);
    CHECK(api->has("game.inventory.contents.read"));
    CHECK(api->query_service(SC_CONTAINER_CONTENTS_NAME, SC_CONTAINER_CONTENTS_VERSION_1_0, &raw) == SCO_OK);
    sco::sdk::game::ContainerContents consumer(static_cast<const sc_container_contents_v1*>(raw), self);
    recursive_table = static_cast<const sc_container_contents_v1*>(raw); recursive_self = self;
    out = Output();
    CHECK(consumer.snapshot(42, out) == SCO_OK);
    CHECK(out.count == 2 && out.total_seen == 2 && out.generation == 7 && out.used_volume == 16);
    CHECK(out.items[0].quantity == 2 && out.items[1].item_id == 102);
    sco::caps::Set("game.inventory.contents.read", false, "fixture revoke");
    out = Output(); CHECK(consumer.snapshot(42, out) == SCO_UNAVAILABLE && out.count == 0);
    sco::caps::Set("game.inventory.contents.read", true, "fixture restore");
    out = Output(1);
    CHECK(consumer.snapshot(42, out) == SCO_OK && out.count == 1 && out.total_seen == 2);
    CHECK(out.flags & SC_CONTAINER_CONTENTS_TRUNCATED);
    out = Output(0);
    CHECK(consumer.snapshot(42, out) == SCO_OK && out.count == 0 && out.total_seen == 2);
    const unsigned before = provider.calls;
    out = Output(65); CHECK(consumer.snapshot(42, out) == SCO_BAD_ARG && provider.calls == before);
    out = Output(); CHECK(consumer.snapshot(0, out) == SCO_BAD_ARG);
    CHECK(sc_container_contents_read(table, nullptr, 42, &out) == SCO_BAD_ARG);
    out = Output(); --out.size; CHECK(consumer.snapshot(42, out) == SCO_BAD_ARG);
    sco_result thread_result = SCO_OK;
    std::thread thread([&] { auto other = Output(); thread_result = consumer.snapshot(42, other); }); thread.join();
    CHECK(thread_result == SCO_WRONG_THREAD);
    for (int mode = 1; mode <= 9; ++mode) {
        provider.mode = mode; out = Output();
        const auto r = consumer.snapshot(42, out);
        if (mode == 1) CHECK(r == SCO_NOT_FOUND && out.count == 0);
        else if (mode == 3) CHECK(r == SCO_OK && out.count == 0 && out.total_seen == 0);
        else if (mode == 9) CHECK(r == SCO_OK && out.total_seen == 5 && (out.flags & SC_CONTAINER_CONTENTS_TRUNCATED));
        else CHECK(r == SCO_FAILED && out.count == 0 && out.generation == 0);
    }
    provider.mode = 0;
    provider.mode = 17; out = Output();
    CHECK(consumer.snapshot(42, out) == SCO_UNAVAILABLE && out.count == 0);
    sco::caps::Set("game.inventory.contents.read", true, "fixture restore");
    provider.mode = 0;
    provider.mode = 10; out = Output();
    CHECK(consumer.snapshot(42, out) == SCO_OK && recursive_result == SCO_FAILED);
    for (int mode = 11; mode <= 16; ++mode) {
        provider.mode = mode; out = Output();
        CHECK(consumer.snapshot(42, out) == SCO_FAILED && out.count == 0);
    }
    provider.mode = 0;
    struct Extended { sc_container_snapshot s; unsigned char tail[16]; } extended{};
    extended.s = Output(); extended.s.size = sizeof(extended); std::memset(extended.tail, 0xA5, 16);
    CHECK(consumer.snapshot(42, extended.s) == SCO_OK && extended.s.size == sizeof(extended));
    for (auto b : extended.tail) CHECK(b == 0xA5);
    auto* released = sco::host::NewPlugin("contents_released");
    CHECK(sco::Release(released) == sco::Result::Ok);
    out = Output(); CHECK(sc_container_contents_read(table, released, 42, &out) == SCO_BAD_ARG);
    out = Output(); CHECK(sc_container_contents_read(table, reinterpret_cast<sco_plugin*>(uintptr_t{1}), 42, &out) == SCO_BAD_ARG);
    G::StopContainerContents();
    CHECK(!api->has("game.inventory.contents.read"));
    CHECK(api->query_service(SC_CONTAINER_CONTENTS_NAME, SC_CONTAINER_CONTENTS_VERSION_1_0, &raw) == SCO_NOT_FOUND);
    out = Output(); CHECK(consumer.snapshot(42, out) == SCO_UNAVAILABLE);
    CHECK(G::ConfigureProductionContainerContents({&provider, nullptr}) == sco::Result::BadArg);
    CHECK(G::ConfigureProductionContainerContents({&provider, Read}) == sco::Result::Ok);
    CHECK(G::StartProductionContainerContents() == sco::Result::Ok);
    out = Output(); CHECK(consumer.snapshot(42, out) == SCO_OK);
    G::StopContainerContents();
    CHECK(G::StartProductionContainerContents() == sco::Result::Ok);
    out = Output(); CHECK(consumer.snapshot(42, out) == SCO_UNAVAILABLE);
    G::StopContainerContents();
    std::printf("container contents: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
