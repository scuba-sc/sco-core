// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "sco/reserved_session.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <future>
#include <thread>
#include <utility>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <link.h>
#include <unistd.h>
#endif

#ifndef SCO_CURATED_DRAIN_TESTING
#error "This scratch prototype requires its test-only reader drain observation"
#endif
namespace sco::reserved::curated_testing { bool ReaderDrainObserved() noexcept; }
namespace rs = sco::reserved;
namespace fs = std::filesystem;
namespace {
const rs::MockTable* g_mockTable = nullptr;
void Check(bool ok, const char* expression, int line) {
    if (ok) return;
    std::fprintf(stderr, "CHECK failed at line %d: %s\n", line, expression);
    std::abort();
}
#define CHECK(expr) Check(static_cast<bool>(expr), #expr, __LINE__)

#if defined(_WIN32)
using ModuleHandle = HMODULE;
#else
using ModuleHandle = void*;
#endif

template<class Function>
Function LoadFunction(ModuleHandle module, const char* name) {
#if defined(_WIN32)
    FARPROC raw = GetProcAddress(module, name);
    if (!raw) {
        std::fprintf(stderr, "GetProcAddress(%s) failed: %lu\n", name, static_cast<unsigned long>(GetLastError()));
        std::abort();
    }
#else
    dlerror();
    void* raw = dlsym(module, name);
    const char* error = dlerror();
    if (error || !raw) {
        std::fprintf(stderr, "dlsym(%s): %s\n", name, error ? error : "null symbol");
        std::abort();
    }
#endif
    static_assert(sizeof(Function) == sizeof(raw), "function and loader symbol pointers must match on this target");
    Function function{};
    std::memcpy(&function, &raw, sizeof(function));
    return function;
}

fs::path ExecutablePath() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    CHECK(length > 0 && length < buffer.size());
    buffer.resize(length);
    return fs::path(buffer);
#else
    std::array<char, PATH_MAX + 1> buffer{};
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), PATH_MAX);
    CHECK(length > 0 && static_cast<std::size_t>(length) <= PATH_MAX);
    buffer[static_cast<std::size_t>(length)] = '\0';
    return fs::path(buffer.data());
#endif
}

fs::path SelectedModulePath() {
#if defined(_WIN32)
    return ExecutablePath().parent_path() / L"curated_module.dll";
#else
    return ExecutablePath().parent_path() / "libcurated_module.so";
#endif
}

ModuleHandle OpenSelectedModule(const fs::path& path) {
#if defined(_WIN32)
    HMODULE module = LoadLibraryW(path.c_str());
    if (!module) {
        std::fwprintf(stderr, L"LoadLibraryW(%ls) failed: %lu\n", path.c_str(), static_cast<unsigned long>(GetLastError()));
        std::abort();
    }
    return module;
#else
    void* module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        std::fprintf(stderr, "dlopen(%s): %s\n", path.c_str(), dlerror());
        std::abort();
    }
    return module;
#endif
}

void ValidateLoadedModule(ModuleHandle module, const fs::path& selectedPath) {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    CHECK(length > 0 && length < buffer.size());
    buffer.resize(length);
    std::error_code error;
    CHECK(fs::equivalent(selectedPath, fs::path(buffer), error) && !error);
#else
    link_map* image = nullptr;
    CHECK(dlinfo(module, RTLD_DI_LINKMAP, &image) == 0 && image && image->l_name);
    std::error_code error;
    CHECK(fs::equivalent(selectedPath, fs::path(image->l_name), error) && !error);
#endif
}

bool WaitFor(const rs::MockTable& table, uint64_t ticket, uint64_t expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        uint64_t value = 0, completed = 0;
        if (table.queued_progress(table.context, &value, &completed) == sco::Result::Ok && completed >= ticket)
            return value == expected;
        std::this_thread::yield();
    }
    return false;
}
} // namespace

#if defined(_WIN32)
extern "C" const rs::MockTable* reserved_host_mock_table() noexcept {
#else
extern "C" __attribute__((visibility("default")))
const rs::MockTable* reserved_host_mock_table() noexcept {
#endif
    return g_mockTable;
}

int main() {
    const fs::path modulePath = SelectedModulePath();
    CHECK(fs::exists(modulePath)); // exact sibling artifact selected by this prototype build
    ModuleHandle module = OpenSelectedModule(modulePath); // intentionally never unloaded
    ValidateLoadedModule(module, modulePath);

    const rs::Bootstrap* bootstrap = rs::CreateExecutableAuthority();
    CHECK(bootstrap);
    rs::CoreBinding binding;
    CHECK(rs::BindCoreInstance(bootstrap, binding) == sco::Result::Ok);

    rs::MockGraph graph{};
    graph.zoneCount = 1;
    graph.zones[0].id = 10;
    std::strcpy(graph.zones[0].name, "prototype-zone");
    graph.entityCount = 1;
    graph.entities[0].id = 42;
    graph.entities[0].zone = 10;
    graph.entities[0].position[0] = 3.25;
    std::strcpy(graph.entities[0].name, "prototype-entity");
    rs::Session session;
    CHECK(rs::ReserveSession(binding, graph, session) == sco::Result::Ok);
    graph.entities[0].position[0] = 999.0; // retained graph must be an independent value copy

    rs::StartupControl startup;
    CHECK(rs::BeginStartup(session, startup) == sco::Result::Ok);
    CHECK(rs::PublishTypedProvider(startup) == sco::Result::Ok);
    CHECK(rs::FinishStartup(startup) == sco::Result::Ok);
    const rs::MockTable* table = rs::QueryMock(session);
    CHECK(table && table->version == rs::kMockVersion);
    g_mockTable = table;

    using Identity = const char* (*)() noexcept;
    using HasAccessor = bool (*)() noexcept;
    using Table = const rs::MockTable* (*)() noexcept;
    using Read = sco::Result (*)(uint64_t, rs::MockEntity*) noexcept;
    using Move = sco::Result (*)(uint64_t, double) noexcept;
    using Enqueue = sco::Result (*)(uint64_t, uint64_t*) noexcept;
    using GateCommand = void (*)() noexcept;
    using GateWait = bool (*)(uint64_t) noexcept;
    const auto moduleIdentity = LoadFunction<Identity>(module, "curated_module_identity");
    const auto moduleHasAccessor = LoadFunction<HasAccessor>(module, "curated_module_host_accessor_found");
    const auto moduleTable = LoadFunction<Table>(module, "curated_module_table");
    const auto moduleRead = LoadFunction<Read>(module, "curated_module_read");
    const auto moduleMove = LoadFunction<Move>(module, "curated_module_move");
    const auto moduleSet = LoadFunction<Enqueue>(module, "curated_module_enqueue_set");
    const auto moduleAdd = LoadFunction<Enqueue>(module, "curated_module_enqueue_add");
    const auto armMoveGate = LoadFunction<GateCommand>(module, "curated_module_arm_move_gate");
    const auto waitMoveGate = LoadFunction<GateWait>(module, "curated_module_wait_move_gate");
    const auto releaseMoveGate = LoadFunction<GateCommand>(module, "curated_module_release_move_gate");
    const rs::MockTable* moduleTableSnapshot = nullptr;
    {
        rs::CallPin hostAdmission;
        CHECK(rs::TryEnter(session, hostAdmission) == sco::Result::Ok);
        CHECK(std::strcmp(moduleIdentity(), "sco305-curated-module-v1") == 0);
        CHECK(moduleHasAccessor());
        moduleTableSnapshot = moduleTable();
        CHECK(moduleTableSnapshot == table);
    }

    rs::MockEntity entity{};
    {
        rs::CallPin hostAdmission;
        CHECK(rs::TryEnter(session, hostAdmission) == sco::Result::Ok);
        CHECK(moduleRead(42, &entity) == sco::Result::Ok && entity.position[0] == 3.25);
    }
    {
        // The outer pin covers both table calls inside moduleMove, its output/result handling,
        // and its unwind. The gate below exercises Close racing that admitted invocation.
        rs::CallPin hostAdmission;
        CHECK(rs::TryEnter(session, hostAdmission) == sco::Result::Ok);
        CHECK(moduleMove(42, 7.5) == sco::Result::Ok);
        CHECK(table->get_entity(table->context, 42, &entity) == sco::Result::Ok && entity.position[0] == 7.5);
    }

    CHECK(table->start_worker(table->context) == sco::Result::Ok);
    uint64_t setTicket = 0, addTicket = 0;
    {
        rs::CallPin hostAdmission;
        CHECK(rs::TryEnter(session, hostAdmission) == sco::Result::Ok);
        CHECK(moduleSet(17, &setTicket) == sco::Result::Ok && setTicket != 0);
    }
    CHECK(WaitFor(*table, setTicket, 17));
    {
        rs::CallPin hostAdmission;
        CHECK(rs::TryEnter(session, hostAdmission) == sco::Result::Ok);
        CHECK(moduleAdd(8, &addTicket) == sco::Result::Ok && addTicket > setTicket);
    }
    CHECK(WaitFor(*table, addTicket, 25));

    {
        rs::CallPin hostAdmission;
        CHECK(rs::TryEnter(session, hostAdmission) == sco::Result::Ok);
        armMoveGate();
    }
    auto inFlightMove = std::async(std::launch::async, [&] {
        rs::CallPin hostAdmission;
        if (rs::TryEnter(session, hostAdmission) != sco::Result::Ok) return sco::Result::Unavailable;
        return moduleMove(42, 9.0);
    });
    {
        rs::CallPin hostAdmission;
        CHECK(rs::TryEnter(session, hostAdmission) == sco::Result::Ok);
        CHECK(waitMoveGate(3000));
    }
    CHECK(rs::Close(session) == sco::Result::Ok);
    CHECK(rs::Status(session) == rs::Phase::Closing);
    rs::CallPin deniedAfterClose;
    CHECK(rs::TryEnter(session, deniedAfterClose) == sco::Result::Unavailable && !deniedAfterClose);
    auto retiring = std::async(std::launch::async, [&] {
        return rs::RetireUntil(session, std::chrono::steady_clock::now() + std::chrono::seconds(4));
    });
    const auto drainDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!rs::curated_testing::ReaderDrainObserved() && std::chrono::steady_clock::now() < drainDeadline)
        std::this_thread::yield();
    // The authority observed a live reader in Closing inside Retire. The outer
    // module pin is still held; neither retirement nor module call can finish.
    CHECK(rs::curated_testing::ReaderDrainObserved());
    CHECK(retiring.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
    CHECK(inFlightMove.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
    CHECK(rs::Status(session) == rs::Phase::Closing);
    // Test-only gate release: this module helper touches no table/context; it lets the already
    // admitted function reach its second table call, which must be refused after Close.
    releaseMoveGate();
    CHECK(inFlightMove.get() == sco::Result::Unavailable);
    CHECK(retiring.get() == rs::RetireResult::QuiescedRetained);
    CHECK(rs::Status(session) == rs::Phase::QuiescedRetained);

    // Refusal probes intentionally run without a host admission after Close/retirement.
    entity.id = 900;
    entity.position[0] = 123.0;
    CHECK(moduleRead(42, &entity) == sco::Result::Unavailable);
    CHECK(entity.id == 900 && entity.position[0] == 123.0);
    uint64_t refusedTicket = 456;
    CHECK(moduleSet(1, &refusedTicket) == sco::Result::Unavailable && refusedTicket == 456);
    CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + std::chrono::seconds(1)) == rs::RetireResult::Refused);
    entity.id = 901;
    entity.position[0] = 124.0;
    CHECK(moduleRead(42, &entity) == sco::Result::Unavailable);
    CHECK(entity.id == 901 && entity.position[0] == 124.0);

    std::printf("PASS: selected module=%s; module table=%p host table=%p; entity x=3.25->7.5; queue tickets %llu->%llu value=25; Close blocked Retire until admitted module call ended; post-Close entry refused\n",
        modulePath.string().c_str(), static_cast<const void*>(moduleTableSnapshot), static_cast<const void*>(table),
        static_cast<unsigned long long>(setTicket), static_cast<unsigned long long>(addTicket));
    return 0;
}
