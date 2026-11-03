// SPDX-License-Identifier: GPL-3.0-only
#ifndef SCOSDK_GAME_INPUT_HPP
#define SCOSDK_GAME_INPUT_HPP

#include "../plugin.hpp"
#include "sc_input.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace sco::sdk::game {

// Caller-owned copy of one action record. id remains numeric so a consumer built
// against an older SDK can still represent ids added by a newer compatible host.
struct InputAction {
    uint32_t id = SC_ACTION_INVALID;
    uint32_t flags = 0;
    std::array<char, SC_INPUT_ACTION_NAME_BYTES> name{};

    std::string_view Name() const noexcept {
        size_t length = 0;
        while (length < name.size() && name[length] != '\0') ++length;
        return {name.data(), length};
    }
};

// Read-only C++ view of game.input's copied action-name registry.
// The API and plugin handles are borrowed and must remain live while this object is used.
// No provider table or game pointer is retained; each call rechecks the capability and
// re-queries the versioned service. All methods follow the C service's game-thread rule.
class InputRegistry {
public:
    sco_result Open(const Plugin& plugin) noexcept { return Open(plugin.Api(), plugin.Self()); }

    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        api_ = nullptr;
        self_ = nullptr;
        if (!api || !self) return SCO_BAD_ARG;
        const sc_input_v1* table = nullptr;
        const sco_result result = QueryTable(api, table);
        if (result != SCO_OK) return result;
        api_ = api;
        self_ = self;
        return SCO_OK;
    }

    explicit operator bool() const noexcept {
        const sc_input_v1* table = nullptr;
        return api_ && self_ && QueryTable(api_, table) == SCO_OK;
    }

    // On failure, count is unchanged.
    sco_result Count(uint32_t& count) const noexcept {
        const sc_input_v1* table = nullptr;
        const sco_result available = CurrentTable(table);
        if (available != SCO_OK) return available;
        uint32_t copied = 0;
        const sco_result result = table->count(self_, &copied);
        if (result != SCO_OK) return result;
        count = copied;
        return SCO_OK;
    }

    // On success, out owns its copied name and numeric id. On failure, out is unchanged.
    sco_result ById(uint32_t id, InputAction& out) const noexcept {
        const sc_input_v1* table = nullptr;
        const sco_result available = CurrentTable(table);
        if (available != SCO_OK) return available;
        sc_input_action_info raw{};
        raw.size = sizeof(raw);
        const sco_result result = table->by_id(self_, id, &raw);
        if (result != SCO_OK) return result;
        InputAction copied{};
        if (!CopyAction(raw, copied) || copied.id != id) return SCO_FAILED;
        out = copied;
        return SCO_OK;
    }

    // Accepts a bounded, exact, case-sensitive name. On success, out owns its
    // copied name and numeric id. On failure, out is unchanged.
    sco_result ByName(std::string_view name, InputAction& out) const noexcept {
        if (name.empty() || name.size() >= SC_INPUT_ACTION_NAME_BYTES ||
            std::memchr(name.data(), '\0', name.size())) return SCO_BAD_ARG;
        std::array<char, SC_INPUT_ACTION_NAME_BYTES> query{};
        std::memcpy(query.data(), name.data(), name.size());

        const sc_input_v1* table = nullptr;
        const sco_result available = CurrentTable(table);
        if (available != SCO_OK) return available;
        sc_input_action_info raw{};
        raw.size = sizeof(raw);
        const sco_result result = table->by_name(self_, query.data(), &raw);
        if (result != SCO_OK) return result;
        InputAction copied{};
        if (!CopyAction(raw, copied) || copied.Name() != name) return SCO_FAILED;
        out = copied;
        return SCO_OK;
    }

private:
    static constexpr const char* kCapability = "game.input.registry";

    static bool CompleteTable(const sc_input_v1* table) noexcept {
        return table &&
            Covers(table->size, offsetof(sc_input_v1, count), sizeof(table->count)) && table->count &&
            Covers(table->size, offsetof(sc_input_v1, by_id), sizeof(table->by_id)) && table->by_id &&
            Covers(table->size, offsetof(sc_input_v1, by_name), sizeof(table->by_name)) && table->by_name;
    }

    static sco_result QueryTable(const sco_api* api, const sc_input_v1*& table) noexcept {
        table = nullptr;
        if (!api || !Covers(api->size, offsetof(sco_api, has), sizeof(api->has)) || !api->has ||
            !Covers(api->size, offsetof(sco_api, query_service), sizeof(api->query_service)) ||
            !api->query_service || !api->has(kCapability)) return SCO_UNAVAILABLE;
        const void* raw = nullptr;
        const sco_result result = api->query_service(SC_INPUT_SERVICE_NAME, SC_INPUT_SERVICE_VERSION, &raw);
        if (result != SCO_OK) return result;
        const auto* candidate = static_cast<const sc_input_v1*>(raw);
        if (!CompleteTable(candidate)) return SCO_UNAVAILABLE;
        table = candidate;
        return SCO_OK;
    }

    sco_result CurrentTable(const sc_input_v1*& table) const noexcept {
        table = nullptr;
        if (!api_ || !self_) return SCO_UNAVAILABLE;
        return QueryTable(api_, table);
    }

    static bool CopyAction(const sc_input_action_info& raw, InputAction& out) noexcept {
        if (raw.size != sizeof(raw) || raw.id == SC_ACTION_INVALID) return false;
        const auto* terminator = static_cast<const char*>(std::memchr(raw.name, '\0', sizeof(raw.name)));
        if (!terminator || terminator == raw.name) return false;
        InputAction copied{};
        copied.id = raw.id;
        copied.flags = raw.flags;
        const size_t length = static_cast<size_t>(terminator - raw.name);
        std::memcpy(copied.name.data(), raw.name, length);
        out = copied;
        return true;
    }

    const sco_api* api_ = nullptr;
    sco_plugin* self_ = nullptr;
};

}  // namespace sco::sdk::game

#endif  // SCOSDK_GAME_INPUT_HPP
