// Synthetic only: these values are invented and are not game build evidence.
#include "../src/game/exact_build_profile.h"
#include "sco/net/sha2.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>
using namespace sco::game::profile;
static int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static Profile Fixture() {
    return {1, "synthetic-a", 1, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "fixture-a",
            {"5d3e0b356ddd211c42ed3ee716b8927c53ef2158fa5f530af73269b92613b2a9", 41, 34404, 123, 4096},
            {9, 8, 7, 6}};
}
static std::string Digest(const std::string& bytes) {
    uint8_t hash[32]; sco_sha256_digest(bytes.data(), bytes.size(), hash);
    const char* hex = "0123456789abcdef"; std::string out;
    for (auto byte : hash) { out += hex[byte >> 4]; out += hex[byte & 15]; }
    return out;
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    auto p = Fixture(); Binding binding{"fixture-a", 1}; Observation o{p.executable, p.build};
    auto select = [&](const Profile& candidate, const Observation& observation) {
        return Select(std::span(&candidate, 1), std::span(&binding, 1), observation).status;
    };
    CHECK(select(p, o) == Status::Selected);
    auto result = Select(std::span(&p, 1), std::span(&binding, 1), o);
    CHECK(result.profile_index == 0 && result.binding_id == 1);
    CHECK(SelectProduction(o).status == Status::UnknownExecutable);
    CHECK(Select({}, {}, o).status == Status::UnknownExecutable);
    auto bad = p; bad.schema = 2; CHECK(select(bad, o) == Status::InvalidCatalog);
    bad = p; bad.profile_id = "-bad"; CHECK(select(bad, o) == Status::InvalidCatalog);
    bad = p; bad.profile_id = "bad\\\""; CHECK(select(bad, o) == Status::InvalidCatalog);
    std::string long_id(65, 'a'); bad = p; bad.profile_id = long_id; CHECK(select(bad, o) == Status::InvalidCatalog);
    bad = p; bad.revision = 0; CHECK(select(bad, o) == Status::InvalidCatalog);
    bad = p; bad.pack_source_sha = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"; CHECK(select(bad, o) == Status::InvalidCatalog);
    bad = p; bad.table_set = "unknown"; CHECK(select(bad, o) == Status::InvalidCatalog);
    bad = p; bad.executable.file_size = 0; CHECK(select(bad, o) == Status::InvalidCatalog);
    bad = p; bad.executable.machine = 0x14c; CHECK(select(bad, o) == Status::InvalidCatalog);
    bad = p; bad.executable.pe_timestamp = 0; CHECK(select(bad, o) == Status::InvalidCatalog);
    bad = p; bad.executable.size_of_image = 0; CHECK(select(bad, o) == Status::InvalidCatalog);
    bad = p; bad.executable.sha256 = "abc"; CHECK(select(bad, o) == Status::InvalidCatalog);
    Profile profiles[] = {p, p};
    CHECK(Select(profiles, std::span(&binding, 1), o).status == Status::InvalidCatalog);
    profiles[1].profile_id = "other"; CHECK(Select(profiles, std::span(&binding, 1), o).status == Status::InvalidCatalog);
    profiles[1].executable.sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    profiles[1].profile_id = p.profile_id; CHECK(Select(profiles, std::span(&binding, 1), o).status == Status::InvalidCatalog);
    profiles[1].profile_id = "other"; profiles[1].schema = 0;
    CHECK(Select(profiles, std::span(&binding, 1), o).status == Status::InvalidCatalog);
    Binding bindings[] = {{"fixture-a", 1}, {"fixture-a", 2}};
    CHECK(Select(std::span(&p, 1), bindings, o).status == Status::InvalidCatalog);
    bindings[1] = {"fixture-b", 1}; CHECK(Select(std::span(&p, 1), bindings, o).status == Status::InvalidCatalog);
    bindings[1] = {"fixture-b", 0}; CHECK(Select(std::span(&p, 1), bindings, o).status == Status::InvalidCatalog);
    auto mismatch = o; mismatch.executable.sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    CHECK(select(p, mismatch) == Status::UnknownExecutable);
    mismatch = o; ++mismatch.executable.file_size; CHECK(select(p, mismatch) == Status::IdentityMismatch);
    mismatch = o; ++mismatch.executable.pe_timestamp; CHECK(select(p, mismatch) == Status::IdentityMismatch);
    mismatch = o; ++mismatch.executable.size_of_image; CHECK(select(p, mismatch) == Status::IdentityMismatch);
    mismatch = o; mismatch.executable.machine = 0x14c; CHECK(select(p, mismatch) == Status::InvalidObservation);
    for (size_t i = 0; i < 4; ++i) { mismatch = o; ++mismatch.build[i]; CHECK(select(p, mismatch) == Status::IdentityMismatch); }
    mismatch = o; mismatch.executable.sha256 = "bad"; CHECK(select(p, mismatch) == Status::InvalidObservation);
    std::string canonical;
    CHECK(Canonical(p, canonical));
    if (argc == 2) {
        std::ifstream fixture(argv[1], std::ios::binary);
        CHECK(fixture.good());
        std::string expected{std::istreambuf_iterator<char>(fixture), std::istreambuf_iterator<char>()};
        CHECK(canonical == expected);
    }
    CHECK(Digest(canonical) == "b455ec55c68f81715f74b6e1cec2f7b12a3c91970f49cdd5dcd02c5323d9d392");
    bad = p; bad.schema = 0; CHECK(!Canonical(bad, canonical) && canonical.empty());
    // Profile views may alias the output. Canonicalization must finish all reads first.
    std::string alias = "synthetic-a"; bad = p; bad.profile_id = alias;
    CHECK(Canonical(bad, alias) && Digest(alias) == "b455ec55c68f81715f74b6e1cec2f7b12a3c91970f49cdd5dcd02c5323d9d392");
    alias = "INVALID"; bad.profile_id = alias;
    CHECK(!Canonical(bad, alias) && alias.empty());
    bad = p; bad.revision = std::numeric_limits<uint32_t>::max(); bad.executable.file_size = std::numeric_limits<uint64_t>::max();
    CHECK(Canonical(bad, canonical) && canonical.find("18446744073709551615") != std::string::npos);
    std::printf("exact build profile: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
