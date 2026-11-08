#include "exact_build_profile.h"

namespace sco::game::profile {
namespace {
bool Token(std::string_view s) {
    if (s.empty() || s.size() > 64) return false;
    auto first = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (!first(s[0])) return false;
    for (char c : s) if (!first(c) && c != '.' && c != '_' && c != '-') return false;
    return true;
}
bool Hex(std::string_view s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
bool ValidExecutable(const Executable& x) {
    return Hex(x.sha256, 64) && x.file_size && x.machine == 0x8664 && x.pe_timestamp && x.size_of_image;
}
bool Valid(const Profile& p) {
    return p.schema == 1 && Token(p.profile_id) && p.revision && Hex(p.pack_source_sha, 40) &&
           Token(p.table_set) && ValidExecutable(p.executable);
}
Selection Fail(Status status) { return {status, 0, 0}; }
}  // namespace

Selection Select(std::span<const Profile> profiles, std::span<const Binding> bindings,
                 const Observation& observed) {
    // Validate even unused entries. A malformed or ambiguous catalog must not select a prefix.
    for (size_t i = 0; i < bindings.size(); ++i) {
        if (!Token(bindings[i].table_set) || !bindings[i].id) return Fail(Status::InvalidCatalog);
        for (size_t j = 0; j < i; ++j)
            if (bindings[i].table_set == bindings[j].table_set || bindings[i].id == bindings[j].id)
                return Fail(Status::InvalidCatalog);
    }
    for (size_t i = 0; i < profiles.size(); ++i) {
        const auto& p = profiles[i];
        if (!Valid(p)) return Fail(Status::InvalidCatalog);
        bool bound = false;
        for (const auto& b : bindings) if (p.table_set == b.table_set) bound = true;
        if (!bound) return Fail(Status::InvalidCatalog);
        for (size_t j = 0; j < i; ++j)
            if (p.profile_id == profiles[j].profile_id || p.executable.sha256 == profiles[j].executable.sha256)
                return Fail(Status::InvalidCatalog);
    }
    if (!ValidExecutable(observed.executable)) return Fail(Status::InvalidObservation);
    for (size_t i = 0; i < profiles.size(); ++i) {
        const auto& p = profiles[i];
        if (p.executable.sha256 != observed.executable.sha256) continue;
        if (!(p.executable == observed.executable) || p.build != observed.build)
            return Fail(Status::IdentityMismatch);
        for (const auto& b : bindings)
            if (p.table_set == b.table_set) return {Status::Selected, i, b.id};
    }
    return Fail(Status::UnknownExecutable);
}
Selection SelectProduction(const Observation& observed) { return Select({}, {}, observed); }

bool Canonical(const Profile& p, std::string& out) {
    out.clear();
    if (!Valid(p)) return false;
    // All text is constrained to unescaped ASCII by Valid(). Sorted keys, decimal integers.
    out = "{\"executable\":{\"file_size\":" + std::to_string(p.executable.file_size) +
          ",\"machine\":" + std::to_string(p.executable.machine) +
          ",\"pe_timestamp\":" + std::to_string(p.executable.pe_timestamp) +
          ",\"sha256\":\"" + std::string(p.executable.sha256) +
          "\",\"size_of_image\":" + std::to_string(p.executable.size_of_image) +
          "},\"manifest\":{\"build\":[" + std::to_string(p.build[0]) + "," +
          std::to_string(p.build[1]) + "," + std::to_string(p.build[2]) + "," +
          std::to_string(p.build[3]) + "]},\"pack_source_sha\":\"" + std::string(p.pack_source_sha) +
          "\",\"profile_id\":\"" + std::string(p.profile_id) + "\",\"revision\":" +
          std::to_string(p.revision) + ",\"schema\":" + std::to_string(p.schema) +
          ",\"table_set\":\"" + std::string(p.table_set) + "\"}";
    return true;
}
}  // namespace sco::game::profile
