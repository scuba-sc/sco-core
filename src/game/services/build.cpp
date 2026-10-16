#include "build.h"
#include "sco/host.h"
#include "sco/scan.h"
#include <sc_game_pack.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace sco::game::services {
namespace {
sc_game_build_v1 g_table = {sizeof(sc_game_build_v1), 0, nullptr};
bool PinConsistent() {
    char b[64];  int n = std::snprintf(b, sizeof(b), "%u.%u.%u.%u",
        SCO_GAME_PACK_VERIFIED_BUILD_PARTS_0, SCO_GAME_PACK_VERIFIED_BUILD_PARTS_1,
        SCO_GAME_PACK_VERIFIED_BUILD_PARTS_2, SCO_GAME_PACK_VERIFIED_BUILD_PARTS_3);
    if (n < 0 || std::strcmp(b, SCO_GAME_PACK_VERIFIED_BUILD) != 0) return false;
    char t[32];  n = std::snprintf(t, sizeof(t), "0x%x",
        (unsigned)std::strtoull(SCO_GAME_PACK_VERIFIED_PE_TIMESTAMP, nullptr, 16));
    return n > 0 && std::strcmp(t, SCO_GAME_PACK_VERIFIED_PE_TIMESTAMP) == 0;
}
sco_result Read(sco_plugin* self, sc_game_build_info* out) {
    (void)self;
    if (!out || out->size < sizeof(sc_game_build_info)) return SCO_UNAVAILABLE;
    const Image img = ModuleImage();
    if (!img.base || !img.timestamp) return SCO_UNAVAILABLE;
    const uint32_t size = out->size;
    std::memset(out, 0, sizeof(*out));
    out->size = size;
    out->pe_timestamp = img.timestamp;
    out->size_of_image = img.size;
    out->pack_major = SCO_GAME_PACK_VERSION_MAJOR;
    out->pack_minor = SCO_GAME_PACK_VERSION_MINOR;
    out->pack_patch = SCO_GAME_PACK_VERSION_PATCH;
    out->verified_build[0] = SCO_GAME_PACK_VERIFIED_BUILD_PARTS_0;
    out->verified_build[1] = SCO_GAME_PACK_VERIFIED_BUILD_PARTS_1;
    out->verified_build[2] = SCO_GAME_PACK_VERIFIED_BUILD_PARTS_2;
    out->verified_build[3] = SCO_GAME_PACK_VERIFIED_BUILD_PARTS_3;
    out->verified_pe_timestamp =
        (uint32_t)std::strtoull(SCO_GAME_PACK_VERIFIED_PE_TIMESTAMP, nullptr, 16);
    out->pin_matches = out->pe_timestamp == out->verified_pe_timestamp ? 1u : 0u;
    return SCO_OK;
}
}  // namespace
Result StartGameBuild() {
    if (g_table.read) return Result::Failed;
    if (!PinConsistent()) return Result::Failed;
    g_table.read = Read;
    const Result r = host::ProvideGameService(SC_GAME_BUILD_SERVICE_NAME,
                                              SC_GAME_BUILD_SERVICE_VERSION, &g_table);
    if (r != Result::Ok) g_table.read = nullptr;
    return r;
}
void StopGameBuild() {
    if (!g_table.read) return;
    host::WithdrawGameService(SC_GAME_BUILD_SERVICE_NAME);
    g_table.read = nullptr;
}
}  // namespace sco::game::services
