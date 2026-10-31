// SPDX-License-Identifier: GPL-3.0-only
#include "../src/lifecycle_gate.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); std::abort(); } } while (0)

using namespace std::chrono_literals;
using sco::LifecycleGate;

int main() {
    LifecycleGate gate;
    auto reader = gate.try_enter();
    CHECK(reader);
    std::mutex mutex;
    std::condition_variable cv;
    bool started = false, release = false, cleaned = false;
    std::thread closer([&] {
        { std::lock_guard lock(mutex); started = true; }
        cv.notify_all();
        bool ok = gate.close_until(std::chrono::steady_clock::now() + 10s, [&] {
            auto nested = gate.try_cleanup_enter();
            CHECK(nested);
            CHECK(!gate.try_enter());
            CHECK(!gate.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));
            std::thread async([&] { CHECK(!gate.try_enter()); CHECK(!gate.try_cleanup_enter()); });
            async.join();
            { std::unique_lock lock(mutex); cleaned = true; cv.notify_all(); cv.wait(lock, [&] { return release; }); }
            return true;
        });
        CHECK(ok);
    });
    { std::unique_lock lock(mutex); cv.wait(lock, [&] { return started; }); }
    CHECK(gate.wait_until_closed(std::chrono::steady_clock::now() + 10s));
    CHECK(!gate.try_enter());
    CHECK(!gate.try_cleanup_enter());
    CHECK(!gate.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));
    reader = {};
    { std::unique_lock lock(mutex); cv.wait(lock, [&] { return cleaned; }); release = true; }
    cv.notify_all(); closer.join();
    CHECK(!gate.try_enter()); CHECK(!gate.try_cleanup_enter());

    LifecycleGate timeout;
    auto held = timeout.try_enter(); CHECK(held);
    bool called = false;
    CHECK(!timeout.close_until(std::chrono::steady_clock::now(), [&] { called = true; return true; }));
    CHECK(!called); CHECK(!timeout.try_enter());
    held = {};
    CHECK(!timeout.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));

    LifecycleGate failure;
    CHECK(!failure.close_until(std::chrono::steady_clock::now() + 1s, [] { return false; }));
    CHECK(!failure.try_enter());
    CHECK(!failure.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));

    LifecycleGate throwing;
    try {
        throwing.close_until(std::chrono::steady_clock::now() + 1s, [&]() -> bool {
            auto nested = throwing.try_cleanup_enter(); CHECK(nested);
            throw std::runtime_error("cleanup");
        });
        CHECK(false);
    } catch (const std::runtime_error&) {}
    CHECK(!throwing.try_enter()); CHECK(!throwing.try_cleanup_enter());
    CHECK(!throwing.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));

    LifecycleGate faulted;
    auto live = faulted.try_enter(); CHECK(live);
    faulted.fault();
    CHECK(!faulted.try_enter());
    CHECK(!faulted.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));
    live = {};
    CHECK(!faulted.try_cleanup_enter());

    LifecycleGate draining_fault;
    auto draining_reader = draining_fault.try_enter(); CHECK(draining_reader);
    std::thread fault_closer([&] {
        CHECK(!draining_fault.close_until(std::chrono::steady_clock::now() + 10s, [] { CHECK(false); return true; }));
    });
    CHECK(draining_fault.wait_until_closed(std::chrono::steady_clock::now() + 10s));
    draining_fault.fault();
    fault_closer.join();
    draining_reader = {};
    CHECK(!draining_fault.try_enter());

    LifecycleGate cleanup_fault;
    CHECK(!cleanup_fault.close_until(std::chrono::steady_clock::now() + 1s, [&] {
        CHECK(cleanup_fault.try_cleanup_enter());
        cleanup_fault.fault();
        CHECK(!cleanup_fault.try_cleanup_enter());
        return true;
    }));
    CHECK(!cleanup_fault.try_enter());

    LifecycleGate unwind_reader;
    try { auto lease = unwind_reader.try_enter(); CHECK(lease); throw 1; } catch (int) {}
    CHECK(unwind_reader.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));
}
