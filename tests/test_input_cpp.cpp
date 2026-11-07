// SPDX-License-Identifier: GPL-3.0-only
#include <scosdk/game/input.hpp>

#include <cstdio>
#include <cstring>
#include <string_view>

namespace game = sco::sdk::game;

static int g_pass = 0;
static int g_fail = 0;
#define CHECK(...) do { if (__VA_ARGS__) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } } while (0)

static int g_self_storage = 0;
static sco_plugin* const g_self = reinterpret_cast<sco_plugin*>(&g_self_storage);
static sco_api g_api{};
static sc_input_v1 g_table{};
static const sc_input_v1* g_query_table = &g_table;
static bool g_capability = true;
static sco_result g_query_result = SCO_OK;
static sco_result g_count_result = SCO_OK;
static sco_result g_by_id_result = SCO_OK;
static sco_result g_by_name_result = SCO_OK;
static int g_record_mode = 0;
static int g_query_calls = 0;
static int g_method_calls = 0;
static const char* g_by_id_name = "v_pitch";

static int Has(const char* capability) {
    return capability && std::strcmp(capability, "game.input.registry") == 0 && g_capability;
}

static sco_result QueryService(const char* name, uint32_t version, const void** out) {
    ++g_query_calls;
    if (!name || !out) return SCO_BAD_ARG;
    *out = nullptr;
    if (std::strcmp(name, SC_INPUT_SERVICE_NAME) != 0 || version != SC_INPUT_SERVICE_VERSION)
        return SCO_NOT_FOUND;
    if (g_query_result != SCO_OK) return g_query_result;
    *out = g_query_table;
    return SCO_OK;
}

static void Fill(sc_input_action_info* out, uint32_t id, const char* name) {
    const uint32_t size = out->size;
    *out = {};
    out->size = size;
    out->id = id;
    out->flags = 7;
    if (g_record_mode == 1) {
        std::memset(out->name, 'x', sizeof(out->name)); // Malformed: no terminator.
    } else {
        std::strncpy(out->name, name, sizeof(out->name) - 1);
    }
    if (g_record_mode == 2) out->size = 0;
    if (g_record_mode == 3) out->id = id + 1;
}

static sco_result Count(sco_plugin* self, uint32_t* out) {
    ++g_method_calls;
    if (self != g_self || !out) return SCO_BAD_ARG;
    if (g_count_result != SCO_OK) {
        *out = 3; // The wrapper must not publish a provider's partial failure output.
        return g_count_result;
    }
    *out = SC_INPUT_ACTION_COUNT;
    return SCO_OK;
}

static sco_result ById(sco_plugin* self, uint32_t id, sc_input_action_info* out) {
    ++g_method_calls;
    if (self != g_self || !out) return SCO_BAD_ARG;
    if (g_by_id_result != SCO_OK) {
        out->id = 3;
        return g_by_id_result;
    }
    Fill(out, id, g_by_id_name);
    return SCO_OK;
}

static sco_result ByName(sco_plugin* self, const char* name, sc_input_action_info* out) {
    ++g_method_calls;
    if (self != g_self || !name || !out) return SCO_BAD_ARG;
    if (g_by_name_result != SCO_OK) {
        out->id = 3;
        return g_by_name_result;
    }
    Fill(out, SC_ACTION_V_PITCH, g_record_mode == 4 ? "different_name" : name);
    return SCO_OK;
}

static game::InputAction SentinelAction() {
    game::InputAction value{};
    value.id = 77;
    value.flags = 55;
    std::strcpy(value.name.data(), "sentinel");
    return value;
}

static bool IsSentinel(const game::InputAction& value) {
    return value.id == 77 && value.flags == 55 && value.Name() == "sentinel";
}

int main() {
    g_api.size = sizeof(g_api);
    g_api.major = 1;
    g_api.minor = 1;
    g_api.has = Has;
    g_api.query_service = QueryService;
    g_table = {sizeof(g_table), 0, Count, ById, ByName};

    game::InputRegistry registry;
    CHECK(registry.Open(nullptr, g_self) == SCO_BAD_ARG);
    CHECK(registry.Open(&g_api, nullptr) == SCO_BAD_ARG);

    g_api.size = offsetof(sco_api, has);
    CHECK(registry.Open(&g_api, g_self) == SCO_UNAVAILABLE);
    g_api.size = offsetof(sco_api, query_service);
    CHECK(registry.Open(&g_api, g_self) == SCO_UNAVAILABLE);
    g_api.size = sizeof(g_api);

    g_capability = false;
    const int queriesBeforeCapability = g_query_calls;
    CHECK(registry.Open(&g_api, g_self) == SCO_UNAVAILABLE);
    CHECK(g_query_calls == queriesBeforeCapability);
    g_capability = true;

    g_query_result = SCO_NOT_FOUND;
    CHECK(registry.Open(&g_api, g_self) == SCO_NOT_FOUND);
    g_query_result = SCO_OK;

    g_query_table = nullptr;
    CHECK(registry.Open(&g_api, g_self) == SCO_UNAVAILABLE);
    g_query_table = &g_table;
    const uint32_t fullTableSize = g_table.size;
    g_table.size = static_cast<uint32_t>(offsetof(sc_input_v1, by_name));
    CHECK(registry.Open(&g_api, g_self) == SCO_UNAVAILABLE);
    g_table.size = fullTableSize;
    const auto byName = g_table.by_name;
    g_table.by_name = nullptr;
    CHECK(registry.Open(&g_api, g_self) == SCO_UNAVAILABLE);
    g_table.by_name = byName;

    CHECK(registry.Open(&g_api, g_self) == SCO_OK);
    CHECK(static_cast<bool>(registry));
    uint32_t count = 41;
    CHECK(registry.Count(count) == SCO_OK && count == SC_INPUT_ACTION_COUNT);
    g_count_result = SCO_NOT_FOUND;
    count = 41;
    CHECK(registry.Count(count) == SCO_NOT_FOUND && count == 41);
    g_count_result = SCO_OK;

    auto action = SentinelAction();
    CHECK(registry.ById(SC_ACTION_V_PITCH, action) == SCO_OK);
    CHECK(action.id == SC_ACTION_V_PITCH && action.flags == 7 && action.Name() == "v_pitch");
    const auto ownedAction = action;
    g_by_id_name = "changed_after_copy";
    CHECK(ownedAction.Name() == "v_pitch" && action.Name() == "v_pitch");
    g_by_id_name = "v_pitch";

    g_query_table = nullptr; // A service table is resolved again for each call.
    count = 41;
    CHECK(registry.Count(count) == SCO_UNAVAILABLE && count == 41);
    g_query_table = &g_table;

    g_by_id_result = SCO_NOT_FOUND;
    action = SentinelAction();
    CHECK(registry.ById(SC_ACTION_V_PITCH, action) == SCO_NOT_FOUND && IsSentinel(action));
    g_by_id_result = SCO_OK;
    g_record_mode = 1;
    action = SentinelAction();
    CHECK(registry.ById(SC_ACTION_V_PITCH, action) == SCO_FAILED && IsSentinel(action));
    g_record_mode = 2;
    CHECK(registry.ById(SC_ACTION_V_PITCH, action) == SCO_FAILED && IsSentinel(action));
    g_record_mode = 3;
    CHECK(registry.ById(SC_ACTION_V_PITCH, action) == SCO_FAILED && IsSentinel(action));
    g_record_mode = 0;

    CHECK(registry.ByName("v_pitch", action) == SCO_OK);
    CHECK(action.id == SC_ACTION_V_PITCH && action.Name() == "v_pitch");
    const int callsBeforeInvalidNames = g_method_calls;
    CHECK(registry.ByName({}, action) == SCO_BAD_ARG);
    const char embeddedNull[] = {'v', '\0', 'p'};
    CHECK(registry.ByName(std::string_view(embeddedNull, sizeof(embeddedNull)), action) == SCO_BAD_ARG);
    const char tooLong[SC_INPUT_ACTION_NAME_BYTES] = {};
    CHECK(registry.ByName(std::string_view(tooLong, sizeof(tooLong)), action) == SCO_BAD_ARG);
    CHECK(g_method_calls == callsBeforeInvalidNames);
    g_record_mode = 4;
    action = SentinelAction();
    CHECK(registry.ByName("v_pitch", action) == SCO_FAILED && IsSentinel(action));
    g_record_mode = 0;
    g_by_name_result = SCO_NOT_FOUND;
    CHECK(registry.ByName("v_pitch", action) == SCO_NOT_FOUND && IsSentinel(action));
    g_by_name_result = SCO_OK;

    g_capability = false; // Capability changes are observed after Open too.
    count = 41;
    CHECK(registry.Count(count) == SCO_UNAVAILABLE && count == 41);
    CHECK(!static_cast<bool>(registry));
    g_capability = true;

    g_query_result = SCO_NOT_FOUND; // Re-opening clears any previously usable state.
    CHECK(registry.Open(&g_api, g_self) == SCO_NOT_FOUND);
    count = 41;
    CHECK(registry.Count(count) == SCO_UNAVAILABLE && count == 41);

    std::printf("input_cpp: %d checks, %d failures\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
