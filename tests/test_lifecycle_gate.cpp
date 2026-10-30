// SPDX-License-Identifier: GPL-3.0-only
#include "../src/lifecycle_gate.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
using sco::LifecycleGate;

int main() {
    LifecycleGate gate;
    auto reader = gate.try_enter();
    assert(reader);
    std::mutex mutex;
    std::condition_variable cv;
    bool started = false, release = false, cleaned = false;
    std::thread closer([&] {
        { std::lock_guard lock(mutex); started = true; }
        cv.notify_all();
        bool ok = gate.close_until(std::chrono::steady_clock::now() + 10s, [&] {
            auto nested = gate.try_cleanup_enter();
            assert(nested);
            assert(!gate.try_enter());
            assert(!gate.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));
            std::thread async([&] { assert(!gate.try_enter()); assert(!gate.try_cleanup_enter()); });
            async.join();
            { std::unique_lock lock(mutex); cleaned = true; cv.notify_all(); cv.wait(lock, [&] { return release; }); }
            return true;
        });
        assert(ok);
    });
    { std::unique_lock lock(mutex); cv.wait(lock, [&] { return started; }); }
    assert(gate.wait_until_closed(std::chrono::steady_clock::now() + 10s));
    assert(!gate.try_enter());
    assert(!gate.try_cleanup_enter());
    assert(!gate.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));
    reader = {};
    { std::unique_lock lock(mutex); cv.wait(lock, [&] { return cleaned; }); release = true; }
    cv.notify_all(); closer.join();
    assert(!gate.try_enter()); assert(!gate.try_cleanup_enter());

    LifecycleGate timeout;
    auto held = timeout.try_enter(); assert(held);
    bool called = false;
    assert(!timeout.close_until(std::chrono::steady_clock::now(), [&] { called = true; return true; }));
    assert(!called); assert(!timeout.try_enter());
    held = {};
    assert(!timeout.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));

    LifecycleGate failure;
    assert(!failure.close_until(std::chrono::steady_clock::now() + 1s, [] { return false; }));
    assert(!failure.try_enter());
    assert(!failure.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));

    LifecycleGate throwing;
    try {
        throwing.close_until(std::chrono::steady_clock::now() + 1s, [&]() -> bool {
            auto nested = throwing.try_cleanup_enter(); assert(nested);
            throw std::runtime_error("cleanup");
        });
        assert(false);
    } catch (const std::runtime_error&) {}
    assert(!throwing.try_enter()); assert(!throwing.try_cleanup_enter());
    assert(!throwing.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));

    LifecycleGate faulted;
    auto live = faulted.try_enter(); assert(live);
    faulted.fault();
    assert(!faulted.try_enter());
    assert(!faulted.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));
    live = {};
    assert(!faulted.try_cleanup_enter());

    LifecycleGate draining_fault;
    auto draining_reader = draining_fault.try_enter(); assert(draining_reader);
    std::thread fault_closer([&] {
        assert(!draining_fault.close_until(std::chrono::steady_clock::now() + 10s, [] { assert(false); return true; }));
    });
    assert(draining_fault.wait_until_closed(std::chrono::steady_clock::now() + 10s));
    draining_fault.fault();
    fault_closer.join();
    draining_reader = {};
    assert(!draining_fault.try_enter());

    LifecycleGate cleanup_fault;
    assert(!cleanup_fault.close_until(std::chrono::steady_clock::now() + 1s, [&] {
        assert(cleanup_fault.try_cleanup_enter());
        cleanup_fault.fault();
        assert(!cleanup_fault.try_cleanup_enter());
        return true;
    }));
    assert(!cleanup_fault.try_enter());

    LifecycleGate unwind_reader;
    try { auto lease = unwind_reader.try_enter(); assert(lease); throw 1; } catch (int) {}
    assert(unwind_reader.close_until(std::chrono::steady_clock::now() + 1s, [] { return true; }));
}
