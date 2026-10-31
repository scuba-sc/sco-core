// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
// Synthetic capacity/allocator regressions. No host globals or native readers.
#include "sco/signature_registry.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>
#ifdef _MSC_VER
#include <malloc.h>
#define CAPACITY_NOINLINE __declspec(noinline)
#else
#define CAPACITY_NOINLINE __attribute__((noinline))
#endif

// Fail one actual allocation on this thread. Re-arm only around the operation
// under test, after constructing fixtures, and disarm before any assertion.
static thread_local std::ptrdiff_t allocationsBeforeFailure = -1;
static void MaybeFail() {
    if (allocationsBeforeFailure < 0) return;
    if (allocationsBeforeFailure-- == 0) {
        allocationsBeforeFailure = -1;
        throw std::bad_alloc();
    }
}
CAPACITY_NOINLINE void* operator new(std::size_t n) {
    MaybeFail();
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
CAPACITY_NOINLINE void* operator new[](std::size_t n) { return ::operator new(n); }
CAPACITY_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
CAPACITY_NOINLINE void operator delete[](void* p) noexcept { ::operator delete(p); }
CAPACITY_NOINLINE void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
CAPACITY_NOINLINE void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
CAPACITY_NOINLINE void* operator new(std::size_t n, std::align_val_t alignment) {
    MaybeFail();
    const auto a = static_cast<std::size_t>(alignment);
#ifdef _MSC_VER
    if (void* p = _aligned_malloc(n ? n : 1, a)) return p;
#else
    void* p = nullptr;
    if (posix_memalign(&p, a < sizeof(void*) ? sizeof(void*) : a, n ? n : 1) == 0) return p;
#endif
    throw std::bad_alloc();
}
CAPACITY_NOINLINE void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
CAPACITY_NOINLINE void operator delete(void* p, std::align_val_t) noexcept {
#ifdef _MSC_VER
    _aligned_free(p);
#else
    std::free(p);
#endif
}
CAPACITY_NOINLINE void operator delete[](void* p, std::align_val_t a) noexcept { ::operator delete(p, a); }
CAPACITY_NOINLINE void operator delete(void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
CAPACITY_NOINLINE void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
using Status = sco::SigRegistryStatus;
static sco::SigResult AtBase(const sco::Image& image, const sco::SigResolveContext&) { return sco::SigOk(image.base); }
static sco::SigResult Reason(const sco::Image&, const sco::SigResolveContext&) { return sco::SigFail("copied allocator failure reason"); }
static sco::Image ImageFor(unsigned char* p) { sco::Image image{}; image.base = p; return image; }
struct Rows {
    std::vector<std::string> ids;
    std::vector<sco::SigDef> defs;
    explicit Rows(size_t n, sco::SigResolver resolve = AtBase) : ids(n), defs(n) {
        for (size_t i = 0; i < n; ++i) ids[i] = "capacity.synthetic.long.identifier." + std::to_string(i);
        for (size_t i = 0; i < n; ++i) defs[i] = {ids[i].c_str(), nullptr, 0, 0, resolve, {}};
    }
    sco::SignatureTable Slice(size_t start, size_t count) const { return {defs.data() + start, count}; }
};

static void TestBoundariesAndGrowth() {
    for (size_t count : {size_t{511}, size_t{512}, size_t{513}, size_t{1501}, size_t{2049}}) {
        Rows rows(count);
        sco::SignatureRegistry registry;
        // Incremental registration forces both row storage and the ID index to
        // grow; single large registration alone would not exercise this path.
        for (size_t start = 0; start < count;) {
            const size_t remaining = count - start;
            const size_t chunk = remaining < 127 ? remaining : 127;
            const auto table = rows.Slice(start, chunk);
            CHECK(registry.RegisterTables(&table, 1).status == Status::Ok);
            start += chunk;
        }
        auto duplicate = rows.Slice(count - 1, 1);
        CHECK(registry.RegisterTables(&duplicate, 1).status == Status::DuplicateId);
        CHECK(registry.Freeze() == Status::Ok);
        unsigned char marker{};
        CHECK(registry.Resolve(ImageFor(&marker)) == Status::Ok);
        const auto snapshot = registry.Snapshot();
        CHECK(snapshot && snapshot->Count() == count);
        for (size_t i = 0; i < count; ++i) {
            CHECK(std::strcmp(snapshot->IdAt(i), rows.ids[i].c_str()) == 0);
            CHECK(snapshot->Lookup(rows.ids[i].c_str()) == snapshot->ResultAt(i));
            CHECK(snapshot->ResultAt(i)->at == &marker);
        }
        CHECK(!snapshot->IdAt(count) && !snapshot->ResultAt(count));
        CHECK(!snapshot->Lookup("capacity.synthetic.absent"));
    }
}

static void TestRegistrationAllocationFailure() {
    Rows rows(513);
    size_t failures = 0;
    bool success = false;
    for (std::ptrdiff_t failAt = 0; failAt < 4096 && !success; ++failAt) {
        sco::SignatureRegistry registry; // Unlimited budget: exercise bad_alloc, not budget refusal.
        const auto seed = rows.Slice(0, 511);
        CHECK(registry.RegisterTables(&seed, 1).status == Status::Ok);
        const auto tail = rows.Slice(511, 2);
        allocationsBeforeFailure = failAt;
        const auto registration = registry.RegisterTables(&tail, 1);
        allocationsBeforeFailure = -1;
        success = registration.status == Status::Ok;
        if (!success) {
            CHECK(registration.status == Status::OutOfMemory);
            ++failures;
            // Retrying the same batch must succeed: a partial commit would
            // leave duplicate IDs. The seed must survive index reallocation.
            CHECK(registry.RegisterTables(&tail, 1).status == Status::Ok);
        }
        CHECK(registry.Freeze() == Status::Ok);
        unsigned char marker{};
        CHECK(registry.Resolve(ImageFor(&marker)) == Status::Ok);
        const auto snapshot = registry.Snapshot();
        CHECK(snapshot && snapshot->Count() == 513);
        for (size_t i = 0; i < 513; ++i) CHECK(snapshot->Lookup(rows.ids[i].c_str())->at == &marker);
    }
    CHECK(success && failures > 0);
    std::printf("registration: %zu allocation failure points, retry preserved 513 rows\n", failures);
}

static void TestResolutionAllocationFailure() {
    Rows rows(513, Reason);
    size_t failures = 0;
    bool success = false;
    for (std::ptrdiff_t failAt = 0; failAt < 4096 && !success; ++failAt) {
        sco::SignatureRegistry registry;
        const auto table = rows.Slice(0, 513);
        CHECK(registry.RegisterTables(&table, 1).status == Status::Ok);
        CHECK(registry.Freeze() == Status::Ok);
        unsigned char first{};
        CHECK(registry.Resolve(ImageFor(&first)) == Status::Ok);
        const auto old = registry.Snapshot();
        const auto* oldResult = old->ResultAt(512);
        const auto* oldReason = oldResult->why;
        const auto used = registry.UsedBytes();
        auto image = std::make_shared<unsigned char>(static_cast<unsigned char>(2));
        auto module = std::make_shared<int>(3);
        std::weak_ptr<unsigned char> imageWeak = image;
        std::weak_ptr<int> moduleWeak = module;
        allocationsBeforeFailure = failAt;
        const auto status = registry.Resolve(ImageFor(image.get()), image, module);
        allocationsBeforeFailure = -1;
        success = status == Status::Ok;
        if (!success) {
            CHECK(status == Status::OutOfMemory);
            ++failures;
            CHECK(registry.Snapshot() == old && registry.UsedBytes() == used);
            image.reset(); module.reset();
            CHECK(imageWeak.expired() && moduleWeak.expired());
            CHECK(registry.Resolve(ImageFor(&first)) == Status::Ok);
        }
        CHECK(old->ResultAt(512) == oldResult && oldResult->why == oldReason);
        CHECK(std::strcmp(oldReason, "copied allocator failure reason") == 0);
        CHECK(old->Lookup(rows.ids[512].c_str()) == oldResult);
    }
    CHECK(success && failures > 513);
    std::printf("resolution: %zu allocation failure points, previous snapshot preserved\n", failures);
}

static void TestPublicationAndLifetime() {
    constexpr size_t count = 2049;
    Rows rows(count);
    auto registry = std::make_unique<sco::SignatureRegistry>();
    const auto table = rows.Slice(0, count);
    CHECK(registry->RegisterTables(&table, 1).status == Status::Ok);
    CHECK(registry->Freeze() == Status::Ok);
    auto first = std::make_shared<unsigned char>(static_cast<unsigned char>(42));
    auto second = std::make_shared<unsigned char>(static_cast<unsigned char>(43));
    auto module = std::make_shared<int>(7);
    std::weak_ptr<unsigned char> firstWeak = first;
    std::weak_ptr<int> moduleWeak = module;
    CHECK(registry->Resolve(ImageFor(first.get()), first, module) == Status::Ok);
    auto held = registry->Snapshot();
    const auto* oldResult = held->ResultAt(512);
    const auto* oldId = held->IdAt(512);
    std::atomic<bool> stop{false};
    std::atomic<unsigned> ready{0}, observedSecond{0};
    std::vector<std::thread> readers;
    for (unsigned r = 0; r < 4; ++r) readers.emplace_back([&] {
        bool announced = false, sawSecond = false;
        do {
            const auto snapshot = registry->Snapshot();
            CHECK(snapshot && snapshot->Count() == count);
            const auto* expected = snapshot->ResultAt(0)->at;
            CHECK(expected == first.get() || expected == second.get());
            for (size_t i = 0; i < count; ++i) {
                CHECK(snapshot->ResultAt(i)->at == expected);
                CHECK(snapshot->Lookup(rows.ids[i].c_str()) == snapshot->ResultAt(i));
            }
            if (!announced) { ready.fetch_add(1); announced = true; }
            if (!sawSecond && expected == second.get()) {
                observedSecond.fetch_add(1); sawSecond = true;
            }
        } while (!stop.load());
    });
    while (ready.load() != 4) std::this_thread::yield();
    for (unsigned i = 0; i < 32; ++i) {
        const auto image = i % 2 ? first : second;
        CHECK(registry->Resolve(ImageFor(image.get()), image, module) == Status::Ok);
        // Each reader must cross a real publication, independent of scheduling.
        if (i == 0) while (observedSecond.load() != 4) std::this_thread::yield();
    }
    stop.store(true);
    for (auto& reader : readers) reader.join();
    // Destruction happens after callers join, as required by the contract.
    registry.reset(); first.reset(); second.reset(); module.reset();
    CHECK(!firstWeak.expired() && !moduleWeak.expired());
    CHECK(held->ResultAt(512) == oldResult && held->IdAt(512) == oldId);
    CHECK(held->Lookup(oldId) == oldResult && *oldResult->at == 42);
    held.reset();
    CHECK(firstWeak.expired() && moduleWeak.expired());
}

int main() {
    TestBoundariesAndGrowth();
    TestRegistrationAllocationFailure();
    TestResolutionAllocationFailure();
    TestPublicationAndLifetime();
    std::puts("signature capacity: boundary, allocation, publication and lifetime checks passed, 0 failed");
}
