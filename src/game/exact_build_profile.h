#pragma once
// Internal, typed representation of the exact-build profile v1 contract. This is not a plugin ABI.
// The caller must supply trustworthy observations and keep catalog/binding storage immutable for
// the whole call. Selection is not proof of the mapped image or authority to activate natives.
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace sco::game::profile {
struct Executable {
    std::string_view sha256;
    uint64_t file_size;
    uint32_t machine, pe_timestamp, size_of_image;
    bool operator==(const Executable&) const = default;
};
struct Profile {
    uint32_t schema;
    std::string_view profile_id;
    uint32_t revision;
    std::string_view pack_source_sha;
    std::string_view table_set;
    Executable executable;
    std::array<uint32_t, 4> build;
};
// IDs are code-owned handles, not addresses or executable code from a profile document.
struct Binding { std::string_view table_set; uint32_t id; };
struct Observation { Executable executable; std::array<uint32_t, 4> build; };
enum class Status { Selected, InvalidCatalog, InvalidObservation, UnknownExecutable, IdentityMismatch };
struct Selection { Status status; size_t profile_index; uint32_t binding_id; };

// Validate the complete catalog and all bindings before selecting: no first-match ambiguity.
Selection Select(std::span<const Profile> profiles, std::span<const Binding> bindings,
                 const Observation& observed);
// Production catalogue is intentionally empty pending reviewed genuine profiles and bindings.
Selection SelectProduction(const Observation& observed);
// Returns false and clears output for an invalid profile. Canonical bytes are not a trust proof.
bool Canonical(const Profile& profile, std::string& output);
}  // namespace sco::game::profile
