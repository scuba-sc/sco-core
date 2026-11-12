// SPDX-License-Identifier: GPL-3.0-only
// Synthetic providers test SDK boundaries without opening a database.
#include "scosdk/storage_metadata.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int passed = 0, failed = 0, calls = 0, mode = 0;
#define CHECK(c) do { if (c) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)
static const sco_storage_v2* provider = nullptr;
static sco_result queryResult = SCO_OK, callResult = SCO_OK;
static sco_plugin* owner = reinterpret_cast<sco_plugin*>(1);
static std::string tableName = "visits";
static sco_result Query(const char* name, uint32_t version, const void** out) {
    CHECK(std::strcmp(name, SCO_STORAGE_V2_NAME) == 0 && version == SCO_STORAGE_V2_VERSION_2_0);
    *out = provider; return queryResult;
}
static sco_result Owner(sco_plugin* self, sco_storage_owner_info* out) {
    ++calls; CHECK(self == owner && out->size == sizeof(*out));
    *out = {sizeof(*out), SCO_STORAGE_OWNER_OPENED, 7, 3, 2};
    if (mode == 1) --out->size;
    if (mode == 2) ++out->size;
    if (mode == 3) out->flags = 8;
    if (mode == 4) out->session_generation = 0;
    return callResult;
}
static sco_result Next(sco_plugin* self, const char*, char* out, uint32_t* size, sco_storage_table_info* info) {
    ++calls; CHECK(self == owner && info->size == sizeof(*info));
    *info = {sizeof(*info), 0, 4};
    if (callResult != SCO_OK) return callResult;
    if (mode == 10) return SCO_TOO_MANY;
    if (mode == 11) { *size = (16u << 20) + 2; return SCO_TOO_MANY; }
    uint32_t needed = static_cast<uint32_t>(tableName.size() + 1);
    if (*size < needed) { *size = needed; return SCO_TOO_MANY; }
    std::memcpy(out, tableName.c_str(), needed); *size = needed;
    if (mode == 1) --info->size;
    if (mode == 2) ++info->size;
    if (mode == 3) info->flags = 1;
    if (mode == 5) *size = 0;
    if (mode == 6) *size = needed + 100000;
    if (mode == 7) out[needed - 1] = 'x';
    if (mode == 8) out[0] = '\0';
    return SCO_OK;
}
static sco_result Rows(sco_plugin* self, const char* table, uint64_t* out) {
    ++calls; CHECK(self == owner && table && std::strcmp(table, "visits") == 0);
    *out = 42; return callResult;
}
static sco_result Quota(sco_plugin* self, sco_storage_quota_info* out) {
    ++calls; CHECK(self == owner && out->size == sizeof(*out));
    *out = {sizeof(*out), 0, 10001, 12288, 4096, 8192};
    if (mode == 1) --out->size;
    if (mode == 2) ++out->size;
    if (mode == 3) out->flags = 1;
    if (mode == 4) out->allocated_bytes = out->effective_bytes + 1;
    if (mode == 5) out->free_bytes = 0;
    return callResult;
}

int main() {
    static_assert(sizeof(sco_storage_owner_info) == 32 && sizeof(sco_storage_table_info) == 16);
    static_assert(sizeof(sco_storage_quota_info) == 40 && sizeof(sco_storage_v2) == 40);
    static_assert(offsetof(sco_storage_v2, owner_info) == 8 && offsetof(sco_storage_v2, next_table) == 16);
    static_assert(offsetof(sco_storage_v2, row_count) == 24 && offsetof(sco_storage_v2, quota_info) == 32);
    sco_api api{}; api.size = sizeof(api); api.query_service = Query;
    sco_storage_v2 table{sizeof(table), 0, Owner, Next, Rows, Quota}; provider = &table;
    sco::sdk::StorageMetadata metadata;
    sco_storage_owner_info oi{}; sco_storage_table_info ti{}; sco_storage_quota_info qi{};
    uint64_t rows = 99; std::string name = "sentinel";
    CHECK(metadata.OwnerInfo(oi) == SCO_UNAVAILABLE);
    CHECK(metadata.Open(nullptr, owner) == SCO_BAD_ARG && metadata.Open(&api, nullptr) == SCO_BAD_ARG);
    CHECK(metadata.Open(&api, owner) == SCO_OK && static_cast<bool>(metadata));
    struct Extended { sco_storage_owner_info info; uint64_t tail; } extended{{999, 0, 0, 0, 0}, 12345};
    CHECK(metadata.OwnerInfo(extended.info) == SCO_OK && extended.info.size == 32 && extended.tail == 12345);
    CHECK(metadata.OwnerInfo(oi) == SCO_OK && oi.session_generation == 7 && oi.table_count == 3 && oi.cursor_count == 2);
    CHECK(metadata.NextTable(nullptr, name, ti) == SCO_OK && name == "visits" && ti.column_count == 4);
    CHECK(metadata.RowCount("visits", rows) == SCO_OK && rows == 42);
    CHECK(metadata.QuotaInfo(qi) == SCO_OK && qi.configured_bytes == 10001 && qi.free_bytes == 8192);
    CHECK(oi.table_count == 3 && ti.column_count == 4); // copied, no provider-owned output pointers
    tableName.assign(900, 'z');
    CHECK(metadata.NextTable("visits", name, ti) == SCO_OK && name == tableName);
    tableName = "";
    CHECK(metadata.NextTable(nullptr, name, ti) == SCO_OK && name.empty());
    tableName = "visits";
    CHECK(metadata.NextTable("visits", name, ti) == SCO_FAILED);
    CHECK(metadata.RowCount(nullptr, rows) == SCO_BAD_ARG);
    for (int bad = 1; bad <= 4; ++bad) {
        mode = bad; oi.table_count = 999;
        CHECK(metadata.OwnerInfo(oi) == SCO_FAILED && oi.table_count == 999);
    }
    for (int bad : {1, 2, 3, 5, 6, 7, 8, 10, 11}) {
        mode = bad; name = "sentinel"; ti.column_count = 999;
        const sco_result expected = bad >= 10 ? SCO_TOO_MANY : SCO_FAILED;
        CHECK(metadata.NextTable(nullptr, name, ti) == expected && name == "sentinel" && ti.column_count == 999);
    }
    for (int bad = 1; bad <= 5; ++bad) {
        mode = bad; qi.free_bytes = 999;
        CHECK(metadata.QuotaInfo(qi) == SCO_FAILED && qi.free_bytes == 999);
    }
    mode = 0;
    for (sco_result error : {SCO_BAD_ARG, SCO_NOT_FOUND, SCO_UNAVAILABLE, SCO_WRONG_THREAD}) {
        callResult = error; rows = 999; oi.table_count = 999; qi.free_bytes = 999; name = "sentinel"; ti.column_count = 999;
        CHECK(metadata.OwnerInfo(oi) == error && oi.table_count == 999);
        CHECK(metadata.NextTable(nullptr, name, ti) == error && name == "sentinel" && ti.column_count == 999);
        CHECK(metadata.RowCount("visits", rows) == error && rows == 999);
        CHECK(metadata.QuotaInfo(qi) == error && qi.free_bytes == 999);
    }
    callResult = SCO_OK;
    // Actual allocations end before each missing slot, including every partial-pointer extent.
    for (uint32_t size = 4; size <= sizeof(table); ++size) {
        void* memory = std::malloc(size); CHECK(memory != nullptr);
        if (!memory) continue;
        std::memset(memory, 0, size); std::memcpy(memory, &size, sizeof(size));
        provider = static_cast<sco_storage_v2*>(memory);
        CHECK(metadata.Open(&api, owner) == SCO_OK);
        calls = 0;
        CHECK(metadata.OwnerInfo(oi) == SCO_UNAVAILABLE);
        CHECK(metadata.NextTable(nullptr, name, ti) == SCO_UNAVAILABLE);
        CHECK(metadata.RowCount("visits", rows) == SCO_UNAVAILABLE);
        CHECK(metadata.QuotaInfo(qi) == SCO_UNAVAILABLE && calls == 0);
        // Fill only complete pointers; earlier slots can work while later ones are absent.
        if (size >= 16) std::memcpy(static_cast<char*>(memory) + 8, &table.owner_info, sizeof(table.owner_info));
        if (size >= 24) std::memcpy(static_cast<char*>(memory) + 16, &table.next_table, sizeof(table.next_table));
        if (size >= 32) std::memcpy(static_cast<char*>(memory) + 24, &table.row_count, sizeof(table.row_count));
        if (size >= 40) std::memcpy(static_cast<char*>(memory) + 32, &table.quota_info, sizeof(table.quota_info));
        CHECK(metadata.OwnerInfo(oi) == (size >= 16 ? SCO_OK : SCO_UNAVAILABLE));
        CHECK(metadata.NextTable(nullptr, name, ti) == (size >= 24 ? SCO_OK : SCO_UNAVAILABLE));
        CHECK(metadata.RowCount("visits", rows) == (size >= 32 ? SCO_OK : SCO_UNAVAILABLE));
        CHECK(metadata.QuotaInfo(qi) == (size >= 40 ? SCO_OK : SCO_UNAVAILABLE));
        std::free(memory);
    }
    provider = &table;
    for (uint32_t size = static_cast<uint32_t>(offsetof(sco_api, query_service)); size < offsetof(sco_api, query_service) + sizeof(api.query_service); ++size) {
        void* memory = std::malloc(size); std::memcpy(memory, &api, size); std::memcpy(memory, &size, sizeof(size));
        CHECK(metadata.Open(static_cast<sco_api*>(memory), owner) == SCO_UNAVAILABLE && !metadata);
        std::free(memory);
    }
    api.query_service = nullptr;
    CHECK(metadata.Open(&api, owner) == SCO_UNAVAILABLE); api.query_service = Query;
    provider = nullptr; CHECK(metadata.Open(&api, owner) == SCO_UNAVAILABLE);
    provider = &table; queryResult = SCO_NOT_FOUND; CHECK(metadata.Open(&api, owner) == SCO_UNAVAILABLE);
    queryResult = SCO_UNAVAILABLE; CHECK(metadata.Open(&api, owner) == SCO_UNAVAILABLE);
    queryResult = SCO_FAILED; CHECK(metadata.Open(&api, owner) == SCO_FAILED);
    std::printf("storage metadata SDK: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
