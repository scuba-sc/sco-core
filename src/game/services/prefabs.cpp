// GPL-3.0-only.
#include "prefabs.h"
#include <cstring>
#include <cmath>
namespace sco::game::services {
bool ValidPrefabPath(const char* path) {
    if (!path) return false;
    size_t n = 0, component = 0;
    for (; n <= SC_PREFABS_MAX_PATH && path[n]; ++n) {
        const unsigned char c = static_cast<unsigned char>(path[n]);
        if (c == '/') {
            const size_t length = n - component;
            if (!length || (length == 1 && path[component] == '.') ||
                (length == 2 && path[component] == '.' && path[component+1] == '.')) return false;
            component = n + 1;
        } else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '{' || c == '}')) return false;
    }
    constexpr char prefix[] = "ObjectContainers/", suffix[] = ".socpak";
    constexpr size_t pre = sizeof(prefix)-1, tail = sizeof(suffix)-1;
    return n <= SC_PREFABS_MAX_PATH && n > pre + tail && component < n - tail &&
        std::memcmp(path, prefix, pre) == 0 && std::memcmp(path+n-tail, suffix, tail) == 0;
}
sco_result SpawnOwnedPrefab(const PrefabProvider& p, sco_plugin* self, const char* path,
    uint64_t zone, const double* pos, const double* rot, uint64_t* out) {
    if (!out) return SCO_BAD_ARG;
    *out = 0;
    if (!p.on_thread || !p.on_thread()) return SCO_WRONG_THREAD;
    if (!self || !p.live || !p.live(self)) return SCO_BAD_ARG;
    if (!ValidPrefabPath(path) || !zone || !pos || !rot) return SCO_BAD_ARG;
    double position[3], q[4];
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(pos[i])) return SCO_BAD_ARG;
        position[i] = pos[i];
    }
    const double norm = std::hypot(std::hypot(rot[0], rot[1]), std::hypot(rot[2], rot[3]));
    if (!std::isfinite(norm) || norm < 1e-9) return SCO_BAD_ARG;
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(rot[i])) return SCO_BAD_ARG;
        q[i] = rot[i] / norm;
    }
    if (!p.ready || !p.ready() || !p.reserve || !p.zone || !p.spawn || !p.remember) return SCO_UNAVAILABLE;
    char content[SC_PREFABS_MAX_PATH+1];
    std::memcpy(content, path, std::strlen(path)+1);
    const auto reserved = p.reserve(self); if (reserved != SCO_OK) return reserved;
    const auto streamed = p.zone(zone); if (streamed != SCO_OK) return streamed;
    uint64_t id = 0;
    const auto spawned = p.spawn(content, zone, position, q, &id);
    if (spawned != SCO_OK) {
        if (id) p.remember(self, id); // cleanup survives a post-spawn batch-release failure
        return spawned;
    }
    if (!id) return SCO_FAILED;
    p.remember(self, id); // reservation above guarantees allocation-free commit
    *out = id;
    return SCO_OK;
}
}
