// Owned signature registry lifecycle and adversarial tests. Synthetic images only.
#include "sco/signature_registry.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
using sco::SigRegistryStatus;
static sco::SigResult AtBase(const sco::Image& i, const sco::SigResolveContext&) { return sco::SigOk(i.base); }
static sco::SigResult UseA(const sco::Image&, const sco::SigResolveContext& c) { return sco::SigOk(c.Sig("a")); }
static char reason[] = "first reason";
static sco::SigResult WithReason(const sco::Image&, const sco::SigResolveContext&) { return sco::SigFail(reason); }
static bool throwNow = false;
static sco::SigResult ThrowSometimes(const sco::Image& i, const sco::SigResolveContext&) {
    if (throwNow) throw std::runtime_error("test");
    return sco::SigOk(i.base);
}
static sco::SignatureRegistry* reentryRegistry;
static SigRegistryStatus reentryResult;
static SigRegistryStatus reentryRegister, reentryFreeze, reentrySeal;
static sco::SigResult Reentry(const sco::Image& i, const sco::SigResolveContext&) {
    reentryResult = reentryRegistry->Resolve(i);
    reentryRegister = reentryRegistry->RegisterTables(nullptr, 0).status;
    reentryFreeze = reentryRegistry->Freeze();
    reentrySeal = reentryRegistry->SealForHost();
    return sco::SigOk(i.base);
}
static sco::Image ImageFor(unsigned char* p) { sco::Image image{}; image.base = p; return image; }

static void TestOwnershipAndAtomicBatch() {
    unsigned char bytes[8]{};
    char id[] = "a", pattern[] = "AA BB", need[] = "a";
    sco::SigDef defs[] = {
        {id, nullptr, 0, 0, AtBase, {}},
        {"b", nullptr, 0, 0, UseA, {need}},
        {"pattern", pattern, 0, 0, nullptr, {}},
        {"reason", nullptr, 0, 0, WithReason, {}}
    };
    sco::SignatureRegistry registry;
    sco::SignatureTable t{defs, 4};
    CHECK(registry.RegisterTables(&t, 1).status == SigRegistryStatus::Ok);
    sco::SigDef duplicates[] = {
        {"before.duplicate", nullptr, 0, 0, AtBase, {}},
        {"a", nullptr, 0, 0, AtBase, {}},
        {"after.duplicate", nullptr, 0, 0, AtBase, {}}
    };
    sco::SignatureTable bad{duplicates, 3};
    auto failure = registry.RegisterTables(&bad, 1);
    CHECK(failure.status == SigRegistryStatus::DuplicateId && failure.table == 0 && failure.row == 1);
    sco::SigDef localDup[] = {{"within", nullptr, 0, 0, AtBase, {}}, {"within", nullptr, 0, 0, AtBase, {}}};
    sco::SignatureTable bad2{localDup, 2};
    CHECK(registry.RegisterTables(&bad2, 1).status == SigRegistryStatus::DuplicateId);
    sco::SigDef invalid[] = {{"valid.prefix", nullptr, 0, 0, AtBase, {}}, {nullptr, nullptr, 0, 0, AtBase, {}}};
    sco::SignatureTable bad3{invalid, 2};
    CHECK(registry.RegisterTables(&bad3, 1).status == SigRegistryStatus::InvalidArgument);
    sco::SigDef other[] = {{"separate.table", nullptr, 0, 0, AtBase, {}}};
    sco::SignatureTable combined[] = {{other, 1}, {duplicates, 3}};
    failure = registry.RegisterTables(combined, 2);
    CHECK(failure.status == SigRegistryStatus::DuplicateId && failure.table == 1 && failure.row == 1);
    CHECK(registry.RegisterTables(nullptr, 1).status == SigRegistryStatus::InvalidArgument);
    CHECK(registry.Snapshot() == nullptr);
    id[0] = 'z'; need[0] = 'z'; pattern[0] = '0';
    CHECK(registry.Freeze() == SigRegistryStatus::Ok);
    CHECK(registry.Snapshot() == nullptr);
    CHECK(registry.RegisterTables(&bad, 1).status == SigRegistryStatus::Frozen);
    bytes[0] = 0xAA; bytes[1] = 0xBB;
    auto image = ImageFor(bytes); image.text = {bytes, sizeof(bytes)};
    CHECK(registry.Resolve(image) == SigRegistryStatus::Ok);
    auto snapshot = registry.Snapshot();
    CHECK(snapshot && snapshot->Count() == 4);
    CHECK(snapshot->Lookup("a") && snapshot->Lookup("a")->at == bytes);
    CHECK(snapshot->Lookup("b") && snapshot->Lookup("b")->at == bytes);
    CHECK(snapshot->Lookup("pattern") && snapshot->Lookup("pattern")->at == bytes);
    CHECK(!snapshot->Lookup("before.duplicate") && !snapshot->Lookup("valid.prefix") && !snapshot->Lookup("within") && !snapshot->Lookup("separate.table"));
    CHECK(!std::strcmp(snapshot->IdAt(0), "a") && !std::strcmp(snapshot->IdAt(3), "reason"));
    CHECK(snapshot->IdAt(4) == nullptr && snapshot->ResultAt(4) == nullptr && snapshot->Lookup(nullptr) == nullptr);
    reason[0] = 'X';
    CHECK(!std::strcmp(snapshot->Lookup("reason")->why, "first reason"));
    reason[0] = 'f';
}
static void TestSnapshotsAndFailure() {
    unsigned char first[1]{}, second[1]{};
    auto registry = std::make_unique<sco::SignatureRegistry>();
    sco::SigDef d[] = {{"row", nullptr, 0, 0, ThrowSometimes, {}}}; sco::SignatureTable t{d, 1};
    CHECK(registry->Resolve(ImageFor(first)) == SigRegistryStatus::NotFrozen);
    CHECK(registry->RegisterTables(&t, 1).status == SigRegistryStatus::Ok);
    CHECK(registry->Freeze() == SigRegistryStatus::Ok);
    CHECK(registry->Resolve(ImageFor(first)) == SigRegistryStatus::Ok);
    auto old = registry->Snapshot();
    CHECK(old && old->Lookup("row")->at == first);
    throwNow = true;
    CHECK(registry->Resolve(ImageFor(second)) == SigRegistryStatus::ResolverException);
    CHECK(registry->Snapshot() == old && old->Lookup("row")->at == first);
    throwNow = false;
    CHECK(registry->Resolve(ImageFor(second)) == SigRegistryStatus::Ok);
    auto next = registry->Snapshot();
    CHECK(next != old && next->Lookup("row")->at == second && old->Lookup("row")->at == first);
    registry.reset();
    CHECK(old->Lookup("row")->at == first && next->Lookup("row")->at == second);
}
static void TestReentry() {
    unsigned char marker{};
    sco::SignatureRegistry registry; reentryRegistry = &registry;
    sco::SigDef d[] = {{"reentry", nullptr, 0, 0, Reentry, {}}}; sco::SignatureTable t{d,1};
    CHECK(registry.RegisterTables(&t,1).status == SigRegistryStatus::Ok);
    CHECK(registry.Freeze() == SigRegistryStatus::Ok);
    CHECK(registry.Resolve(ImageFor(&marker)) == SigRegistryStatus::Ok);
    CHECK(reentryResult == SigRegistryStatus::Busy);
    CHECK(reentryRegister == SigRegistryStatus::Busy && reentryFreeze == SigRegistryStatus::Busy && reentrySeal == SigRegistryStatus::Busy);
    reentryRegistry = nullptr;
}
static void TestHostSeal() {
    unsigned char first{}, second{};
    sco::SignatureRegistry registry;
    CHECK(registry.SealForHost() == SigRegistryStatus::NotFrozen);
    sco::SigDef d[] = {{"seal", nullptr, 0, 0, AtBase, {}}}; sco::SignatureTable t{d, 1};
    CHECK(registry.RegisterTables(&t, 1).status == SigRegistryStatus::Ok);
    CHECK(registry.Freeze() == SigRegistryStatus::Ok);
    CHECK(registry.SealForHost() == SigRegistryStatus::NotResolved);
    CHECK(registry.Resolve(ImageFor(&first)) == SigRegistryStatus::Ok);
    auto old = registry.Snapshot();
    CHECK(registry.SealForHost() == SigRegistryStatus::Ok);
    CHECK(registry.SealForHost() == SigRegistryStatus::Ok);
    CHECK(registry.Resolve(ImageFor(&second)) == SigRegistryStatus::HostSealed);
    CHECK(registry.Snapshot() == old && old->Lookup("seal")->at == &first);
}
static void TestDeepGraph() {
    constexpr size_t n = 20000;
    std::vector<std::string> ids(n); std::vector<sco::SigDef> defs(n);
    for (size_t i = 0; i < n; ++i) ids[i] = "deep." + std::to_string(i);
    for (size_t i = 0; i < n; ++i) defs[i] = {ids[i].c_str(), nullptr, 0, 0, AtBase, {i + 1 < n ? ids[i + 1].c_str() : nullptr}};
    sco::SignatureRegistry registry; sco::SignatureTable t{defs.data(), n}; unsigned char marker{};
    CHECK(registry.RegisterTables(&t,1).status == SigRegistryStatus::Ok);
    CHECK(registry.Freeze() == SigRegistryStatus::Ok);
    CHECK(registry.Resolve(ImageFor(&marker)) == SigRegistryStatus::Ok);
    auto s = registry.Snapshot(); CHECK(s && s->Count() == n && s->Lookup("deep.0")->at == &marker);
}
static void TestBudget() {
    constexpr size_t n = 1200;
    std::vector<std::string> ids(n); std::vector<sco::SigDef> defs(n);
    for (size_t i = 0; i < n; ++i) { ids[i] = "budget.long.identifier." + std::to_string(i); defs[i] = {ids[i].c_str(),nullptr,0,0,AtBase,{}}; }
    sco::SignatureRegistry registry(64 * 1024);
    sco::SigDef seed[] = {{"seed",nullptr,0,0,AtBase,{}}}; sco::SignatureTable one{seed,1}, many{defs.data(),n};
    CHECK(registry.RegisterTables(&one,1).status == SigRegistryStatus::Ok);
    auto failure = registry.RegisterTables(&many,1);
    CHECK(failure.status == SigRegistryStatus::BudgetExceeded);
    CHECK(registry.UsedBytes() <= registry.BudgetBytes());
    CHECK(registry.Freeze() == SigRegistryStatus::Ok);
    unsigned char marker{}; CHECK(registry.Resolve(ImageFor(&marker)) == SigRegistryStatus::Ok);
    auto snapshot = registry.Snapshot();
    CHECK(snapshot && snapshot->Count() == 1 && snapshot->Lookup("seed") && !snapshot->Lookup(ids[0].c_str()));
}
static void TestBudgetDuringPublication() {
    unsigned char first{}, second{};
    bool observed = false;
    for (size_t limit = 1024; limit <= 32768 && !observed; limit += 64) {
        try {
            sco::SignatureRegistry registry(limit);
            sco::SigDef defs[] = {{"one", nullptr, 0, 0, AtBase, {}}, {"two", nullptr, 0, 0, AtBase, {}}};
            sco::SignatureTable t{defs, 2};
            if (registry.RegisterTables(&t, 1).status != SigRegistryStatus::Ok || registry.Freeze() != SigRegistryStatus::Ok || registry.Resolve(ImageFor(&first)) != SigRegistryStatus::Ok) continue;
            auto old = registry.Snapshot();
            auto status = registry.Resolve(ImageFor(&second));
            if (status == SigRegistryStatus::BudgetExceeded) {
                observed = true;
                CHECK(registry.Snapshot() == old && old->Lookup("one")->at == &first);
                CHECK(registry.UsedBytes() <= registry.BudgetBytes());
            }
        } catch (const std::bad_alloc&) { /* Budget too small for registry construction. */ }
    }
    CHECK(observed);
}
static void TestOwnedSnapshots() {
    auto image = std::make_shared<unsigned char>(42);
    auto code = std::make_shared<int>(7); // stand-in for a provider's code-module owner
    std::weak_ptr<unsigned char> imageWeak = image;
    std::weak_ptr<int> codeWeak = code;
    std::shared_ptr<const sco::SignatureSnapshot> held;
    {
        sco::SignatureRegistry registry;
        sco::SigDef d[] = {{"row", nullptr, 0, 0, AtBase, {}}}; sco::SignatureTable t{d, 1};
        CHECK(registry.RegisterTables(&t, 1).status == SigRegistryStatus::Ok);
        CHECK(registry.Freeze() == SigRegistryStatus::Ok);
        CHECK(registry.Resolve(ImageFor(image.get()), {}, code) == SigRegistryStatus::InvalidArgument);
        CHECK(registry.Resolve(ImageFor(image.get()), image, {}) == SigRegistryStatus::InvalidArgument);
        CHECK(registry.Resolve(ImageFor(image.get()), {}, {}) == SigRegistryStatus::InvalidArgument);
        CHECK(!registry.Snapshot());
        CHECK(registry.Resolve(ImageFor(image.get()), image, code) == SigRegistryStatus::Ok);
        held = registry.Snapshot();
        unsigned char replacement{};
        CHECK(registry.Resolve(ImageFor(&replacement)) == SigRegistryStatus::Ok);
        image.reset(); code.reset();
        CHECK(!imageWeak.expired() && !codeWeak.expired());
    }
    CHECK(!imageWeak.expired() && !codeWeak.expired());
    CHECK(held && *held->Lookup("row")->at == 42);
    held.reset();
    CHECK(imageWeak.expired() && codeWeak.expired());
}
static void TestOwnedBudgetFailure() {
    bool observed = false;
    for (size_t limit = 1024; limit <= 32768 && !observed; limit += 64) {
        try {
            sco::SignatureRegistry registry(limit);
            sco::SigDef d[] = {{"one", nullptr, 0, 0, AtBase, {}}, {"two", nullptr, 0, 0, AtBase, {}}};
            sco::SignatureTable t{d, 2};
            auto first = std::make_shared<unsigned char>(1), second = std::make_shared<unsigned char>(2);
            auto code = std::make_shared<int>(1), nextCode = std::make_shared<int>(2);
            if (registry.RegisterTables(&t, 1).status != SigRegistryStatus::Ok || registry.Freeze() != SigRegistryStatus::Ok || registry.Resolve(ImageFor(first.get()), first, code) != SigRegistryStatus::Ok) continue;
            auto old = registry.Snapshot();
            std::weak_ptr<unsigned char> failedImage = second;
            std::weak_ptr<int> failedCode = nextCode;
            const size_t before = registry.UsedBytes();
            auto status = registry.Resolve(ImageFor(second.get()), second, nextCode);
            if (status == SigRegistryStatus::BudgetExceeded) {
                observed = true;
                CHECK(registry.Snapshot() == old && old->Lookup("one")->at == first.get());
                CHECK(registry.UsedBytes() == before);
                second.reset(); nextCode.reset();
                CHECK(failedImage.expired() && failedCode.expired());
            }
        } catch (const std::bad_alloc&) {}
    }
    CHECK(observed);
}
static void TestPublication() {
    constexpr size_t n = 64;
    std::vector<std::string> ids(n); std::vector<sco::SigDef> defs(n);
    for (size_t i = 0; i < n; ++i) { ids[i] = "publication." + std::to_string(i); defs[i] = {ids[i].c_str(),nullptr,0,0,AtBase,{}}; }
    sco::SignatureRegistry registry; sco::SignatureTable t{defs.data(),n}; unsigned char markers[2]{};
    CHECK(registry.RegisterTables(&t,1).status == SigRegistryStatus::Ok);
    CHECK(registry.Freeze() == SigRegistryStatus::Ok);
    CHECK(registry.Resolve(ImageFor(&markers[0])) == SigRegistryStatus::Ok);
    std::atomic<bool> stop{false}; std::atomic<int> errors{0};
    std::vector<std::thread> readers;
    for (int k = 0; k < 4; ++k) readers.emplace_back([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            auto s = registry.Snapshot();
            if (!s || s->Count() != n) { ++errors; continue; }
            auto expected = s->ResultAt(0)->at;
            if (expected != &markers[0] && expected != &markers[1]) ++errors;
            for (size_t i = 0; i < n; ++i) if (s->ResultAt(i)->at != expected) ++errors;
        }
    });
    for (int i = 0; i < 200; ++i) CHECK(registry.Resolve(ImageFor(&markers[i % 2])) == SigRegistryStatus::Ok);
    stop.store(true, std::memory_order_relaxed);
    for (auto& thread : readers) thread.join();
    CHECK(errors.load() == 0);
}
int main() {
    TestOwnershipAndAtomicBatch(); TestSnapshotsAndFailure(); TestReentry(); TestHostSeal(); TestDeepGraph(); TestBudget(); TestBudgetDuringPublication(); TestOwnedSnapshots(); TestOwnedBudgetFailure(); TestPublication();
    std::printf("signature registry: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
