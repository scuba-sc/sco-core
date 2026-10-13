// Synthetic early DataCore load through the real host mount producer.
#include "sco/game/host.h"
#include "../src/game/host_testing.h"
#include "sco/datacore_service.h"
#include "dcb_builder.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace gh = sco::game::host;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static gh::Admission Inspect() { return {gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear}; }
class Base final : public sco::vfs::BaseIo {
public:
    std::vector<uint8_t> bytes = dcb::ValuedFixture(36).Build();
    size_t at = 0;
    bool Seek(uint64_t pos) override { if (pos > bytes.size()) return false; at = static_cast<size_t>(pos); return true; }
    size_t Read(void* p, size_t n) override { n = std::min(n, bytes.size() - at); std::memcpy(p, bytes.data() + at, n); at += n; return n; }
};
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path root = argv[1];
    std::filesystem::remove_all(root);
    const auto pack = root / "plugins" / "synthetic_pack";
    std::filesystem::create_directories(pack / "datacore");
    std::ofstream(pack / "plugin.ini") << "id = synthetic_pack\nname = Synthetic pack\nversion = 1.0.0\napi = 1.1\nkind = data\n";
    std::ofstream(pack / "datacore" / "a.toml") << "format = 1\n[[set]]\nrecord = \"ShipA\"\nfield = \"speed\"\nvalue = 7.0\n";
    uint8_t bytes[8192]{};
    sco::Image image{bytes, {bytes, 4096}, {bytes + 4096, 4096}, {}, 0, sizeof(bytes)};
    gh::Options o;
    o.inspect = Inspect; o.image = &image; o.quit = gh::QuitPolicy::External;
    o.platform.dataRoot = root;
    o.platform.pluginRoot = root / "plugins";
    o.platform.pluginsEnabled = true;
    o.platform.dataCore = true;
    o.dataCoreHook = gh::DataCoreHook::External; // portable transport only; no executable patch
    CHECK(gh::Prepare(o) == sco::Result::Ok);
    CHECK(sco::datacore::service::Started());
    CHECK(sco::datacore::service::Table()->state() == SCO_DC_OPEN);
    Base base;
    std::vector<sco::vfs::Splice> splices;
    // Invalid/bounded data refuses before changing the service's OPEN phase.
    CHECK(!gh::testing::TransformDataCore(base, o.maxDataCoreBytes + 1, splices));
    CHECK(sco::datacore::service::Table()->state() == SCO_DC_OPEN);
    CHECK(gh::testing::TransformDataCore(base, base.bytes.size(), splices));
    CHECK(!splices.empty());
    CHECK(sco::datacore::service::Table()->state() == SCO_DC_LOADED);
    // Start must NOT restart/reset the service after an early loader applied the pack.
    CHECK(gh::Start() == sco::Result::Ok);
    CHECK(sco::datacore::service::Table()->state() == SCO_DC_LOADED);
    CHECK(gh::Status().dataCoreActive);
    CHECK(gh::Stop() == sco::Result::Ok);
    CHECK(!sco::datacore::service::Started() && !gh::Status().dataCoreActive);
    std::vector<sco::vfs::Splice> late;
    CHECK(!gh::testing::TransformDataCore(base, base.bytes.size(), late));
    CHECK(late.empty());
    std::filesystem::remove_all(root);
    std::printf("game_host_data: %d failures\n", failures);
    return failures ? 1 : 0;
}
