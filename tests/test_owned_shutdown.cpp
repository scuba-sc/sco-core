// SPDX-License-Identifier: GPL-3.0-only
#include "sco/owned_shutdown.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <stdexcept>
using namespace std::chrono_literals;
int main() {
    sco::OwnedShutdown domain;
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    std::atomic<int> cachedCalls{0};
    assert(domain.start_worker([&](const auto& stop) {
        auto read = domain.try_read();
        assert(read);
        entered.set_value();
        released.wait();
        ++cachedCalls; // synthetic cached-table read, still inside admitted lifetime
        assert(stop());
    }));
    entered.get_future().wait();
    assert(!domain.drain_until(std::chrono::steady_clock::now() + 1ms));
    assert(!domain.ready());
    assert(!domain.try_read());
    assert(!domain.start_worker([](const auto&) {}));
    release.set_value();
    while (cachedCalls.load() != 1) std::this_thread::yield();
    // A failed drain is terminal: joining later cannot manufacture readiness.
    assert(!domain.drain_until(std::chrono::steady_clock::now() + 1s));
    assert(cachedCalls == 1);
    sco::OwnedShutdown good;
    std::promise<void> started;
    assert(good.start_worker([&](const auto& stop) {
        started.set_value();
        while (!stop()) std::this_thread::yield();
    }));
    started.get_future().wait();
    assert(good.drain_until(std::chrono::steady_clock::now() + 1s));
    assert(good.ready() && !good.try_read());
    assert(!good.start_worker([](const auto&) {}));
    sco::OwnedShutdown throwing;
    assert(throwing.start_worker([](const auto&) { throw std::runtime_error("worker fault"); }));
    assert(!throwing.drain_until(std::chrono::steady_clock::now() + 1s));
    assert(!throwing.ready());
    sco::OwnedShutdown invalidated;
    assert(invalidated.drain_until(std::chrono::steady_clock::now() + 1s));
    invalidated.fault();
    assert(!invalidated.ready());
    // A worker's body return is not join: thread-local destruction can block.
    std::promise<void> tlsEntered, tlsRelease;
    auto tlsReleased = tlsRelease.get_future().share();
    struct Tail {
        std::promise<void>* entered;
        std::shared_future<void> release;
        ~Tail() { entered->set_value(); release.wait(); }
    };
    sco::OwnedShutdown tls;
    assert(tls.start_worker([&](const auto&) {
        thread_local Tail tail{&tlsEntered, tlsReleased};
        (void)tail;
    }));
    tlsEntered.get_future().wait();
    assert(!tls.drain_until(std::chrono::steady_clock::now() + 1ms));
    assert(!tls.ready());
    tlsRelease.set_value();
}
