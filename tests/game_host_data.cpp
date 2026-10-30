// Synthetic early DataCore load through the real host mount producer.
#include "sco/game/host.h"
#include "../src/game/host_testing.h"
#include "sco/datacore_service.h"
#include "sco/signature_registry.h"
#include "sco/caps.h"
#include "dcb_builder.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace gh = sco::game::host;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static gh::Admission evidence{gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
static gh::Admission Inspect() { return evidence; }
static std::string mode;
static int requirementCalls = 0, capabilityCalls = 0, shutdownCalls = 0;
static gh::ConsumerRetirement RetireConsumer(void* context) {
    CHECK(context == &mode && !gh::FreshAdmissionAllowed());
    ++shutdownCalls;
    CHECK(sco::datacore::service::Started()); // retirement runs before early provider teardown
    if (mode == "partial-throw") throw std::runtime_error("synthetic consumer retirement fault");
    if (mode == "partial-retained") return gh::ConsumerRetirement::Retained;
    return gh::ConsumerRetirement::Quiesced; // synthetic fixture: the product has no external readers
}
static std::shared_ptr<sco::SignatureRegistry> successor;
static void SetCapabilities() {
    ++capabilityCalls;
    sco::caps::Set("fixture.early", true);
    if (mode.rfind("partial-", 0) == 0) throw std::runtime_error("synthetic capability startup failure");
}
static bool EarlyRequirement(const char* name, const sco::SignatureSnapshot& snapshot, void* context) {
    ++requirementCalls;
    CHECK(context == &mode && std::strcmp(name, "fixture.early") == 0);
    CHECK(snapshot.Lookup("system.quit") != nullptr);
    const auto owner = sco::AcquireHostSignatureRegistry();
    CHECK(owner && owner->Snapshot().get() == &snapshot);
    if (mode == "throw") throw std::runtime_error("synthetic early requirement fault");
    if (mode == "admission") evidence.session = gh::Session::Online;
    if (mode == "owner") {
        auto hostileOwner = std::const_pointer_cast<sco::SignatureRegistry>(owner); // exercise hostile ownership loss
        CHECK(!sco::ClearHostSignatureRegistry(hostileOwner, nullptr));
        CHECK(!sco::InstallHostSignatureRegistry(successor, nullptr));
        CHECK(sco::AcquireHostSignatureRegistry() == owner);
    }
    return mode != "false";
}
class Base final : public sco::vfs::BaseIo {
public:
    std::vector<uint8_t> bytes = dcb::ValuedFixture(36).Build();
    size_t at = 0;
    bool Seek(uint64_t pos) override { if (pos > bytes.size()) return false; at = static_cast<size_t>(pos); return true; }
    size_t Read(void* p, size_t n) override { n = std::min(n, bytes.size() - at); std::memcpy(p, bytes.data() + at, n); at += n; return n; }
};
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    mode = argc == 3 ? argv[2] : "plain";
    const bool partial = mode.rfind("partial-", 0) == 0;
    if (mode != "plain" && mode != "true" && mode != "false" && mode != "null" && mode != "throw" && mode != "admission" && mode != "owner" &&
        mode != "partial-quiesced" && mode != "partial-retained" && mode != "partial-null" && mode != "partial-throw") return 2;
    const std::filesystem::path root = argv[1];
    std::filesystem::remove_all(root);
    const auto pack = root / "plugins" / "synthetic_pack";
    std::filesystem::create_directories(pack / "datacore");
    std::ofstream manifest(pack / "plugin.ini");
    manifest << "id = synthetic_pack\nname = Synthetic pack\nversion = 1.0.0\napi = 1.1\nkind = data\n";
    if (mode != "plain" && !partial) manifest << "requires = fixture.early\n";
    manifest.close();
    std::ofstream(pack / "datacore" / "a.toml") << "format = 1\n[[set]]\nrecord = \"ShipA\"\nfield = \"speed\"\nvalue = 7.0\n";
    uint8_t bytes[8192]{};
    sco::Image image{bytes, {bytes, 4096}, {bytes + 4096, 4096}, {}, 0, sizeof(bytes)};
    gh::Options o;
    o.inspect = Inspect; o.image = &image; o.quit = gh::QuitPolicy::External;
    auto ownerToken = std::make_shared<int>(1); o.imageOwner = ownerToken; o.resolverOwner = ownerToken;
    o.platform.dataRoot = root;
    o.platform.pluginRoot = root / "plugins";
    o.platform.pluginsEnabled = true;
    o.platform.dataCore = true;
    o.dataCoreHook = gh::DataCoreHook::External; // portable transport only; no executable patch
    o.platform.setCapabilities = SetCapabilities;
    o.retireConsumer = mode == "partial-null" ? nullptr : RetireConsumer;
    o.consumerContext = &mode;
    CHECK(sco::caps::Set("fixture.early", true) == sco::Result::Ok); // stale capability must not authorize early data
    if (mode != "plain" && mode != "null" && !partial) o.earlyDataRequirement = EarlyRequirement;
    o.earlyDataContext = &mode;
    if (mode == "owner") {
        successor = std::make_shared<sco::SignatureRegistry>();
        CHECK(successor->Freeze() == sco::SigRegistryStatus::Ok);
        CHECK(successor->Resolve(image) == sco::SigRegistryStatus::Ok);
    }
    const sco::Result prepared = gh::Prepare(o);
    CHECK(capabilityCalls == 0); // Prepare can precede the game thread
    CHECK(prepared == sco::Result::Ok);
    CHECK(requirementCalls == 0);
    CHECK(!sco::datacore::service::Started());
    const sco::Result activated = gh::Activate();
    CHECK(capabilityCalls == 0);
    if (mode == "admission") {
        CHECK(activated == sco::Result::Unavailable);
        CHECK(gh::Status().phase == gh::Phase::Failed && requirementCalls > 0);
        CHECK(shutdownCalls == 1 && !sco::datacore::service::Started());
        CHECK(!sco::AcquireHostSignatureSnapshot());
        std::filesystem::remove_all(root);
        std::printf("game_host_data %s: %d failures\n", mode.c_str(), failures);
        return failures ? 1 : 0;
    }
    CHECK(activated == sco::Result::Ok);
    CHECK(!sco::caps::Has("fixture.early"));
    CHECK((mode == "plain" || mode == "null" || partial) ? requirementCalls == 0 : requirementCalls > 0);
    CHECK(sco::datacore::service::Started());
    CHECK(sco::datacore::service::Table()->state() == SCO_DC_OPEN);
    if (partial) {
        const auto owner = sco::AcquireHostSignatureRegistry();
        CHECK(gh::Start() == sco::Result::Failed);
        CHECK(capabilityCalls == 1 && shutdownCalls == (mode == "partial-null" ? 0 : 1));
        CHECK(gh::Status().phase == gh::Phase::Failed && gh::Status().dataCoreActive);
        CHECK(sco::datacore::service::Started() && sco::AcquireHostSignatureRegistry() == owner);
        CHECK(!sco::caps::Has("fixture.early") && !gh::FreshAdmissionAllowed());
        auto hostileOwner = std::const_pointer_cast<sco::SignatureRegistry>(owner);
        CHECK(!sco::ClearHostSignatureRegistry(hostileOwner, nullptr));
        CHECK(gh::Start() == sco::Result::BadArg && gh::Stop() == sco::Result::Unavailable);
        // The retained provider can reference this test's data until process exit.
        std::printf("game_host_data %s: %d failures\n", mode.c_str(), failures);
        return failures ? 1 : 0;
    }
    Base base;
    std::vector<sco::vfs::Splice> splices;
    // Invalid/bounded data refuses before changing the service's OPEN phase.
    CHECK(!gh::testing::TransformDataCore(base, o.maxDataCoreBytes + 1, splices));
    CHECK(sco::datacore::service::Table()->state() == SCO_DC_OPEN);
    CHECK(gh::testing::TransformDataCore(base, base.bytes.size(), splices));
    CHECK(splices.empty() == (mode == "false" || mode == "null" || mode == "throw"));
    CHECK(sco::datacore::service::Table()->state() == SCO_DC_LOADED);
    // Start must NOT restart/reset the service after an early loader applied the pack.
    CHECK(gh::Start() == sco::Result::Ok);
    CHECK(capabilityCalls == 1 && sco::caps::Has("fixture.early"));
    CHECK(sco::datacore::service::Table()->state() == SCO_DC_LOADED);
    CHECK(gh::Status().dataCoreActive);
    CHECK(gh::Stop() == sco::Result::Ok);
    CHECK(capabilityCalls == 1 && !sco::caps::Has("fixture.early") && shutdownCalls == 1);
    CHECK(!sco::datacore::service::Started() && !gh::Status().dataCoreActive);
    std::vector<sco::vfs::Splice> late;
    CHECK(!gh::testing::TransformDataCore(base, base.bytes.size(), late));
    CHECK(late.empty());
    std::filesystem::remove_all(root);
    std::printf("game_host_data: %d failures\n", failures);
    return failures ? 1 : 0;
}
