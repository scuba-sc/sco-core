// Synthetic C++ SDK host slots, independent of the host implementation or game.
#include "scosdk/scosdk.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace sdk = sco::sdk;
static int passed = 0, failed = 0;
#define CHECK(...) do { if (__VA_ARGS__) ++passed; else { ++failed; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } } while (0)

struct Table { uint32_t size; };
static const Table table{sizeof(Table)};
static int self_storage;
static auto* const self = reinterpret_cast<sco_plugin*>(&self_storage);
static int calls;
static bool arguments_ok;
static sco_result answer;
static sco_raw_fn registered_fn;
static void* registered_ctx;

static sco_result Provide(sco_plugin* p, const char* name, uint32_t version, const void* data) {
    ++calls;
    arguments_ok = p == self && std::strcmp(name, "test.service") == 0 && version == 0x10002 && data == &table;
    return answer;
}
static sco_result Release(sco_plugin* p, const char* name) {
    ++calls;
    arguments_ok = p == self && std::strcmp(name, "test.service") == 0;
    return answer;
}
static sco_result Register(sco_plugin* p, const char* name, const char* capability, sco_raw_fn fn, void* ctx) {
    ++calls;
    arguments_ok = p == self && std::strcmp(name, "test.raw") == 0 && std::strcmp(capability, "cap") == 0 && fn && ctx;
    if (answer == SCO_OK) { registered_fn = fn; registered_ctx = ctx; }
    return answer;
}
static sco_result Invoke(sco_plugin* p, const char* name, const void* in, uint32_t in_size, void* out, uint32_t* size) {
    ++calls;
    arguments_ok = p == self && std::strcmp(name, "test.raw") == 0 && in && in_size == 1 &&
                   *static_cast<const std::byte*>(in) == std::byte{42} && out && size && *size == 2;
    if (answer == SCO_OK) { static_cast<std::byte*>(out)[0] = std::byte{17}; *size = 1; }
    else *size = 9;
    return answer;
}

static sdk::RawBytesFn Handler() {
    return [](std::span<const std::byte> in, std::span<std::byte> out, uint32_t& written) {
        if (in.size() != 1 || out.size() != 1) return SCO_BAD_ARG;
        out[0] = in[0]; written = 1; return SCO_OK;
    };
}
static sco_result Call(int slot, sdk::Plugin& plugin, uint32_t& written) {
    const std::byte in[]{std::byte{42}};
    std::byte out[2]{};
    switch (slot) {
    case 0: return sdk::Provide(plugin, "test.service", 0x10002, &table);
    case 1: return sdk::Release(plugin, "test.service");
    case 2: return sdk::RegisterRawBytes(plugin, "test.raw", "cap", Handler());
    default: return sdk::InvokeRawBytes(plugin, "test.raw", in, out, written);
    }
}

int main() {
    sco_api full{};
    full.size = sizeof(full);
    full.provide_service = Provide; full.release_service = Release;
    full.register_raw = Register; full.invoke_raw = Invoke;
    const size_t offsets[]{offsetof(sco_api, provide_service), offsetof(sco_api, release_service),
                           offsetof(sco_api, register_raw), offsetof(sco_api, invoke_raw)};
    const size_t widths[]{sizeof(full.provide_service), sizeof(full.release_service),
                          sizeof(full.register_raw), sizeof(full.invoke_raw)};

    // Allocate precisely the advertised extent so ASan detects reads past the table, including
    // partial pointer slots. The minimum still covers the mandatory 1.0 portion for LoadPlugin.
    for (int slot = 0; slot < 4; ++slot) {
        for (size_t extent = offsetof(sco_api, list_commands) + sizeof(full.list_commands);
             extent < offsets[slot] + widths[slot]; ++extent) {
            auto* short_api = static_cast<sco_api*>(std::malloc(extent));
            if (!short_api) return 2;
            std::memcpy(short_api, &full, extent);
            short_api->size = static_cast<uint32_t>(extent);
            sdk::Plugin plugin;
            CHECK(sdk::LoadPlugin(plugin, short_api, self) == SCO_OK);
            calls = 0;
            uint32_t written = 123;
            CHECK(Call(slot, plugin, written) == SCO_UNAVAILABLE);
            CHECK(calls == 0);
            if (slot == 3) CHECK(written == 0);
            sdk::UnloadPlugin(plugin);
            std::free(short_api);
        }
    }

    for (int slot = 0; slot < 4; ++slot) {
        sco_api api = full;
        switch (slot) {
        case 0: api.provide_service = nullptr; break;
        case 1: api.release_service = nullptr; break;
        case 2: api.register_raw = nullptr; break;
        default: api.invoke_raw = nullptr; break;
        }
        sdk::Plugin plugin;
        CHECK(sdk::LoadPlugin(plugin, &api, self) == SCO_OK);
        calls = 0;
        uint32_t written = 123;
        CHECK(Call(slot, plugin, written) == SCO_UNAVAILABLE);
        CHECK(calls == 0);
        if (slot == 3) CHECK(written == 0);
        sdk::UnloadPlugin(plugin);
    }

    sdk::Plugin plugin;
    CHECK(sdk::LoadPlugin(plugin, &full, self) == SCO_OK);
    for (int slot = 0; slot < 4; ++slot) {
        for (sco_result result : {SCO_BAD_ARG, SCO_OK}) {
            answer = result; calls = 0; arguments_ok = false;
            uint32_t written = 123;
            CHECK(Call(slot, plugin, written) == result);
            CHECK(calls == 1 && arguments_ok);
            if (slot == 3) CHECK(written == (result == SCO_OK ? 1u : 9u));
        }
    }
    answer = SCO_OK;
    const std::byte input[]{std::byte{42}};
    std::byte output[2]{};
    uint32_t written = 123;
    CHECK(sdk::InvokeRawBytes(plugin, "test.raw", input, output, written) == SCO_OK &&
          output[0] == std::byte{17} && written == 1);
    std::byte in[]{std::byte{42}}, out[1]{};
    uint32_t size = 1;
    CHECK(registered_fn && registered_fn(in, 1, out, &size, registered_ctx) == SCO_OK && size == 1 && out[0] == in[0]);
    sdk::UnloadPlugin(plugin);

    // A host refusal must not retain the registered closure.
    sdk::Plugin refused;
    CHECK(sdk::LoadPlugin(refused, &full, self) == SCO_OK);
    answer = SCO_BAD_ARG;
    auto owned = std::make_shared<int>(1);
    std::weak_ptr<int> weak = owned;
    CHECK(sdk::RegisterRawBytes(refused, "test.raw", "cap", [owned](auto, auto, auto&) { return SCO_OK; }) == SCO_BAD_ARG);
    owned.reset();
    CHECK(weak.expired());
    sdk::UnloadPlugin(refused);

    sdk::Plugin detached;
    for (int slot = 0; slot < 4; ++slot) {
        uint32_t detached_written = 123;
        CHECK(Call(slot, detached, detached_written) == SCO_BAD_ARG);
        if (slot == 3) CHECK(detached_written == 0);
    }
    std::printf("test_sdk_host_slots: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
