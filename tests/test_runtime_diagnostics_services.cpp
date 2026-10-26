// Synthetic host-only service diagnostics fixture; no game, callbacks or SDK methods.
#include "sco/diagnostics_services.h"
#include "../src/api/internal.h"
// Keep the fixture active in Release builds as well.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

using sco::Result;
using namespace sco::diagnostics;

namespace {

void TestPublicationCopyDoesNotExposeTable() {
    static char owner;
    // Physically one byte: a table-size-prefix probe would be an ASan out-of-bounds read.
    const auto table = std::make_unique<unsigned char[]>(1);
    table[0] = 0xff;
    ResetServiceDiagnosticHistory();
    assert(sco::ProvideService(&owner, nullptr, "fixture.service", 0x00010002, table.get()) == Result::Ok);
    assert(!sco::ServiceTableHandedOut(&owner));
    const auto snapshot = CopyServiceMetadata();
    assert(snapshot.items.size() == 1);
    const auto& item = snapshot.items[0];
    assert(item.service == "fixture.service" && item.packedVersion == 0x00010002);
    assert(!item.ownerId && item.ownerCategory == "unknown");
    assert(item.status == "published" && !item.exposed && !item.tableSize && !item.reason);
    assert(!snapshot.truncated && !sco::ServiceTableHandedOut(&owner));
    assert(sco::ServiceExists("fixture.service"));
    assert(sco::ReleaseService(&owner, "fixture.service") == Result::Ok);
    // An earlier copy is fully owned, even after publication and backing storage disappear.
    assert(item.service == "fixture.service" && item.status == "published");
    ResetServiceDiagnosticHistory();
}

const ServiceMetadata& Find(const ServiceInventory& inventory, const char* name) {
    for (const auto& item : inventory.items)
        if (item.service == name) return item;
    assert(false && "missing service metadata");
    std::abort();
}

void TestTrustedMetadataAndValidation() {
    static char owner, stranger;
    const auto table = std::make_unique<unsigned char[]>(1);
    table[0] = 0xff;
    ResetServiceDiagnosticHistory();
    assert(sco::ProvideService(&owner, nullptr, "fixture.trusted", 0x00020003, table.get()) == Result::Ok);
    char id[] = "fixture_owner";
    assert(SetServiceDiagnosticMetadata(&owner, "fixture.trusted", id, "plugin", 32) == Result::Ok);
    std::memset(id, 'x', sizeof(id) - 1);
    const auto original = CopyServiceMetadata();
    assert(Find(original, "fixture.trusted").ownerId == "fixture_owner");
    assert(Find(original, "fixture.trusted").ownerCategory == "plugin");
    assert(Find(original, "fixture.trusted").tableSize == 32u);
    assert(!sco::ServiceTableHandedOut(&owner));

    assert(SetServiceDiagnosticMetadata(nullptr, "fixture.trusted", "valid", "host", 4) == Result::BadArg);
    assert(SetServiceDiagnosticMetadata(&owner, nullptr, "valid", "host", 4) == Result::BadArg);
    assert(SetServiceDiagnosticMetadata(&owner, "bad..name", "valid", "host", 4) == Result::BadArg);
    const std::string longName(64, 'a');
    assert(SetServiceDiagnosticMetadata(&owner, longName.c_str(), "valid", "host", 4) == Result::BadArg);
    for (const char* invalidId : { "", "Upper", "has.dot", "has/slash", "has space", "has\ncontrol" })
        assert(SetServiceDiagnosticMetadata(&owner, "fixture.trusted", invalidId, "plugin", 32) == Result::BadArg);
    const std::string longId(32, 'a');
    assert(SetServiceDiagnosticMetadata(&owner, "fixture.trusted", longId.c_str(), "plugin", 32) == Result::BadArg);
    char unterminatedId[32];
    std::memset(unterminatedId, 'a', sizeof(unterminatedId));
    assert(SetServiceDiagnosticMetadata(&owner, "fixture.trusted", unterminatedId, "plugin", 32) == Result::BadArg);
    for (const char* invalidCategory : { "", "Plugin", "other", "plugin/path", "plugin\n" })
        assert(SetServiceDiagnosticMetadata(&owner, "fixture.trusted", "valid", invalidCategory, 32) == Result::BadArg);
    char unterminatedCategory[10];
    std::memset(unterminatedCategory, 'x', sizeof(unterminatedCategory));
    assert(SetServiceDiagnosticMetadata(&owner, "fixture.trusted", "valid", unterminatedCategory, 32) == Result::BadArg);
    assert(SetServiceDiagnosticMetadata(&owner, "fixture.trusted", "valid", nullptr, 32) == Result::BadArg);
    for (const uint32_t size : { 65537u, UINT32_MAX })
        assert(SetServiceDiagnosticMetadata(&owner, "fixture.trusted", "valid", "host", size) == Result::BadArg);
    assert(SetServiceDiagnosticMetadata(&stranger, "fixture.trusted", "valid", "host", 4) == Result::NotFound);
    assert(SetServiceDiagnosticMetadata(&owner, "fixture.missing", "valid", "host", 4) == Result::NotFound);
    const auto unchanged = CopyServiceMetadata();
    assert(Find(unchanged, "fixture.trusted").ownerId == "fixture_owner");
    assert(Find(unchanged, "fixture.trusted").ownerCategory == "plugin");
    assert(Find(unchanged, "fixture.trusted").tableSize == 32u);

    const std::string maxId(31, 'a');
    for (const char* category : { "host", "game_pack", "plugin", "unknown" }) {
        assert(SetServiceDiagnosticMetadata(&owner, "fixture.trusted", maxId.c_str(), category, 4) == Result::Ok);
        assert(Find(CopyServiceMetadata(), "fixture.trusted").ownerCategory == category);
    }
    assert(SetServiceDiagnosticMetadata(&owner, "fixture.trusted", nullptr, "unknown", 65536) == Result::Ok);
    assert(Find(CopyServiceMetadata(), "fixture.trusted").tableSize == 65536u);
    assert(SetServiceDiagnosticMetadata(&owner, "fixture.trusted", nullptr, "unknown", std::nullopt) == Result::Ok);
    const auto cleared = CopyServiceMetadata();
    assert(!Find(cleared, "fixture.trusted").ownerId && !Find(cleared, "fixture.trusted").tableSize);
    assert(Find(original, "fixture.trusted").ownerId == "fixture_owner");
    assert(sco::ReleaseService(&owner, "fixture.trusted") == Result::Ok);
    ResetServiceDiagnosticHistory();
}

void TestAdmissionExposureWithdrawalAndReplacement() {
    static char owner, replacement;
    const auto table = std::make_unique<unsigned char[]>(1);
    table[0] = 0xff;
    ResetServiceDiagnosticHistory();
    assert(sco::ProvideService(&owner, nullptr, "fixture.first", 0x00010000, table.get()) == Result::Ok);
    assert(sco::ProvideService(&owner, nullptr, "fixture.second", 0x00010001, table.get()) == Result::Ok);
    assert(SetServiceDiagnosticMetadata(&owner, "fixture.first", "fixture_owner", "host", 64) == Result::Ok);
    const void* const owners[] = { &owner };
    bool inUse = true;
    assert(sco::ServiceWithdrawal(owners, 1, true, false, inUse) == Result::Ok && !inUse);
    const auto reserved = CopyServiceMetadata();
    for (const auto& item : reserved.items)
        assert(item.status == "admission_blocked" && item.reason == "owner_withdrawing" && !item.exposed);
    assert(!sco::ServiceTableHandedOut(&owner));
    assert(sco::ProvideService(&owner, nullptr, "fixture.third", 0x00010000, table.get()) == Result::Unavailable);
    sco::CancelServiceWithdrawal(owners, 1);
    const auto cancelled = CopyServiceMetadata();
    for (const auto& item : cancelled.items)
        assert(item.status == "published" && !item.reason && !item.exposed);

    sco::detail::SetRuntimeStopping(true);
    const auto stopped = CopyServiceMetadata();
    for (const auto& item : stopped.items)
        assert(item.status == "admission_blocked" && item.reason == "runtime_stopping");
    sco::detail::SetRuntimeStopping(false);
    assert(Find(CopyServiceMetadata(), "fixture.first").status == "published");
    const void* out = nullptr;
    // This deliberate hand-out alone changes exposure; diagnostics copies never do.
    assert(sco::QueryService("fixture.first", 0x00010000, &out) == Result::Ok && out == table.get());
    const auto exposed = CopyServiceMetadata();
    assert(Find(exposed, "fixture.first").exposed && Find(exposed, "fixture.second").exposed);
    assert(sco::ServiceWithdrawal(owners, 1, true, false, inUse) == Result::Failed && inUse);
    assert(Find(CopyServiceMetadata(), "fixture.first").status == "published");
    assert(sco::ServiceWithdrawal(owners, 1, true, true, inUse) == Result::Ok && inUse);
    assert(sco::ReleaseService(&owner, "fixture.first") == Result::Ok);
    const auto withdrawn = CopyServiceMetadata();
    const auto& first = Find(withdrawn, "fixture.first");
    assert(first.status == "withdrawn" && first.reason == "service_withdrawn" && first.exposed);
    assert(first.ownerId == "fixture_owner" && first.ownerCategory == "host" && first.tableSize == 64u);
    assert(Find(withdrawn, "fixture.second").status == "admission_blocked");
    assert(SetServiceDiagnosticMetadata(&owner, "fixture.first", "valid", "host", 4) == Result::NotFound);
    assert(!sco::ServiceExists("fixture.first"));
    assert(sco::ProvideService(&replacement, nullptr, "fixture.first", 0x00030000, table.get()) == Result::Ok);
    const auto republished = CopyServiceMetadata();
    assert(republished.items.size() == 2);  // old same-name tombstone is gone
    const auto& fresh = Find(republished, "fixture.first");
    assert(fresh.status == "published" && fresh.packedVersion == 0x00030000 && !fresh.exposed);
    assert(!fresh.ownerId && fresh.ownerCategory == "unknown" && !fresh.tableSize);
    assert(sco::ServiceTableHandedOut(&owner) && !sco::ServiceTableHandedOut(&replacement));
    assert(sco::Release(&owner) == Result::Ok);  // exercises detail::ReleaseServices
    sco::CancelServiceWithdrawal(owners, 1);    // released reservation must stay terminal
    assert(sco::ProvideService(&owner, nullptr, "fixture.third", 0x00010000, table.get()) == Result::BadArg);
    const auto released = CopyServiceMetadata();
    assert(Find(released, "fixture.second").status == "withdrawn");
    assert(Find(released, "fixture.second").exposed);
    assert(sco::ReleaseService(&replacement, "fixture.first") == Result::Ok);
    ResetServiceDiagnosticHistory();
    assert(CopyServiceMetadata().items.empty());
    assert(sco::ServiceTableHandedOut(&owner));  // history reset is not capability reset
}

void TestExposureUpdatesEarlierWithdrawal() {
    static char owner;
    const auto table = std::make_unique<unsigned char[]>(1);
    table[0] = 0xff;
    ResetServiceDiagnosticHistory();
    assert(sco::ProvideService(&owner, nullptr, "fixture.earlier", 0x00010000, table.get()) == Result::Ok);
    assert(sco::ProvideService(&owner, nullptr, "fixture.later", 0x00010000, table.get()) == Result::Ok);
    assert(sco::ReleaseService(&owner, "fixture.earlier") == Result::Ok);
    const auto before = CopyServiceMetadata();
    assert(!Find(before, "fixture.earlier").exposed);
    const void* out = nullptr;
    assert(sco::QueryService("fixture.later", 0x00010000, &out) == Result::Ok && out == table.get());
    const auto after = CopyServiceMetadata();
    assert(Find(after, "fixture.earlier").status == "withdrawn" && Find(after, "fixture.earlier").exposed);
    assert(Find(after, "fixture.later").exposed);
    assert(!Find(before, "fixture.earlier").exposed);  // immutable earlier copy
    assert(sco::ReleaseService(&owner, "fixture.later") == Result::Ok);
    ResetServiceDiagnosticHistory();
}

int g_callouts = 0;
void CountTask(void*) { ++g_callouts; }
void CountEvent(const char*, const void*, void*) { ++g_callouts; }
void CountReleaseHook(const void*) { ++g_callouts; }
Result CountCommand(const sco::Arg*, uint32_t, void*, char*, uint32_t) {
    ++g_callouts;
    return Result::Ok;
}
Result CountRaw(const void*, uint32_t, void*, uint32_t*, void*) {
    ++g_callouts;
    return Result::Ok;
}

void TestCopiesDoNotRunCalloutsOrChangeResources() {
    static char owner;
    const auto table = std::make_unique<unsigned char[]>(1);
    table[0] = 0xff;
    ResetServiceDiagnosticHistory();
    assert(sco::ProvideService(&owner, nullptr, "fixture.observation", 0x00010000, table.get()) == Result::Ok);
    assert(sco::Post(CountTask, nullptr, &owner) == Result::Ok);
    assert(sco::Subscribe(&owner, "fixture.observation", CountEvent, nullptr) == Result::Ok);
    const sco::Command command{ "fixture.observation", "fixture", nullptr, "fixture.capability",
                                nullptr, 0, CountCommand, nullptr };
    assert(sco::RegisterCommand(&owner, nullptr, command) == Result::Ok);
    assert(sco::RegisterRaw(&owner, nullptr, "fixture.observation", "fixture.capability", CountRaw, nullptr) == Result::Ok);
    assert(sco::AddReleaseHook(CountReleaseHook) == Result::Ok);
    const size_t queued = sco::QueuedTasks(), subscriptions = sco::SubscriptionCount();
    const size_t commands = sco::ListCommands(nullptr, 0);
    const void* const owners[] = { &owner };
    bool inUse = true;
    assert(sco::ServiceWithdrawal(owners, 1, true, false, inUse) == Result::Ok && !inUse);
    // The setter and copier also work off the game thread; neither executes provider code.
    std::thread worker([&] {
        assert(SetServiceDiagnosticMetadata(&owner, "fixture.observation", "fixture_owner", "host", 4) == Result::Ok);
        for (unsigned i = 0; i < 16; ++i) {
            const auto copied = CopyServiceMetadata();
            assert(copied.items.size() == 1);
            assert(copied.items[0].status == "admission_blocked" && !copied.items[0].exposed);
        }
        ResetServiceDiagnosticHistory();
    });
    worker.join();
    assert(sco::QueuedTasks() == queued && sco::SubscriptionCount() == subscriptions);
    assert(sco::ListCommands(nullptr, 0) == commands && g_callouts == 0);
    assert(!sco::ServiceTableHandedOut(&owner));
    const auto copied = CopyServiceMetadata();
    assert(Find(copied, "fixture.observation").ownerId == "fixture_owner");
    assert(Find(copied, "fixture.observation").status == "admission_blocked");
    sco::CancelServiceWithdrawal(owners, 1);
    assert(sco::RemoveReleaseHook(CountReleaseHook) == Result::Ok);
    assert(sco::Release(&owner) == Result::Ok);
    assert(g_callouts == 0);  // the queued task was dropped, not executed
    ResetServiceDiagnosticHistory();
}

void TestBoundedHistoryAndInventory() {
    static char owner;
    const auto table = std::make_unique<unsigned char[]>(1);
    table[0] = 0xff;
    ResetServiceDiagnosticHistory();
    for (unsigned i = 0; i < 1024; ++i) {
        const std::string name = "fixture.history_" + std::to_string(i);
        assert(sco::ProvideService(&owner, nullptr, name.c_str(), 0x00010000, table.get()) == Result::Ok);
        assert(sco::ReleaseService(&owner, name.c_str()) == Result::Ok);
    }
    const auto full = CopyServiceMetadata();
    assert(full.items.size() == 1024 && !full.truncated);
    assert(sco::ProvideService(&owner, nullptr, "fixture.newest", 0x00010000, table.get()) == Result::Ok);
    // Active publications take priority over a full history but never cap registry admission.
    const auto combined = CopyServiceMetadata();
    assert(combined.items.size() == 1024 && combined.truncated);
    assert(Find(combined, "fixture.newest").status == "published");
    assert(sco::ReleaseService(&owner, "fixture.newest") == Result::Ok);
    const auto overflow = CopyServiceMetadata();
    assert(overflow.items.size() == 1024 && overflow.truncated);
    assert(Find(overflow, "fixture.newest").status == "withdrawn");
    for (const auto& item : overflow.items) assert(item.service != "fixture.history_0");
    for (unsigned i = 0; i < 16; ++i) {
        assert(sco::ProvideService(&owner, nullptr, "fixture.newest", 0x00010000 + i, table.get()) == Result::Ok);
        const auto replacement = CopyServiceMetadata();
        assert(replacement.items.size() == 1024 && replacement.truncated);
        assert(Find(replacement, "fixture.newest").status == "published");
        assert(sco::ReleaseService(&owner, "fixture.newest") == Result::Ok);
    }
    ResetServiceDiagnosticHistory();
    const auto reset = CopyServiceMetadata();
    assert(reset.items.empty() && !reset.truncated);
    for (unsigned i = 0; i < 1025; ++i) {
        const std::string name = "fixture.active_" + std::to_string(i);
        assert(sco::ProvideService(&owner, nullptr, name.c_str(), 0x00010000, table.get()) == Result::Ok);
    }
    const auto active = CopyServiceMetadata();
    assert(active.items.size() == 1024 && active.truncated);
    assert(sco::ServiceExists("fixture.active_1024"));
    assert(!sco::ServiceTableHandedOut(&owner));
    for (size_t i = 1; i < active.items.size(); ++i)
        assert(active.items[i - 1].service < active.items[i].service);
    assert(sco::detail::ReleaseServices(&owner) == 1025);
    const auto bulkWithdrawn = CopyServiceMetadata();
    assert(bulkWithdrawn.items.size() == 1024 && bulkWithdrawn.truncated);
    ResetServiceDiagnosticHistory();
    assert(CopyServiceMetadata().items.empty() && !CopyServiceMetadata().truncated);
}

}  // namespace

void RunServiceDiagnosticsTests() {
    sco::SetGameThread();
    TestPublicationCopyDoesNotExposeTable();
    TestTrustedMetadataAndValidation();
    TestAdmissionExposureWithdrawalAndReplacement();
    TestExposureUpdatesEarlierWithdrawal();
    TestCopiesDoNotRunCalloutsOrChangeResources();
    TestBoundedHistoryAndInventory();
}
