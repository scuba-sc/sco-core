// SPDX-License-Identifier: GPL-3.0-only
#define SCO_OWNED_SHUTDOWN_TESTING
#include "sco/owned_shutdown.h"
#include "../src/owned_shutdown_access.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); std::abort(); } } while (0)
#include <chrono>
#include <future>
#include <stdexcept>
using namespace std::chrono_literals;

struct BlockingMove {
    std::promise<void>* entered;
    std::shared_future<void> release;
    BlockingMove(std::promise<void>* e, std::shared_future<void> r) : entered(e), release(std::move(r)) {}
    BlockingMove(const BlockingMove&) = delete;
    BlockingMove(BlockingMove&& other) : entered(other.entered), release(other.release) {
        entered->set_value();
        release.wait();
    }
    void operator()(const std::function<bool()>&) {}
};

struct ReentrantMove {
    sco::OwnedShutdown* owner;
    bool* result;
    ReentrantMove(sco::OwnedShutdown* o, bool* r) : owner(o), result(r) {}
    ReentrantMove(const ReentrantMove&) = delete;
    ReentrantMove(ReentrantMove&& other) : owner(other.owner), result(other.result) {
        auto read = owner->try_read();
        *result = static_cast<bool>(read);
    }
    void operator()(const std::function<bool()>&) {}
};

struct ThrowingMove {
    ThrowingMove() = default;
    ThrowingMove(const ThrowingMove&) = delete;
    ThrowingMove(ThrowingMove&&) { throw std::runtime_error("move fault"); }
    void operator()(const std::function<bool()>&) {}
};

struct BlockingDestructor {
    sco::OwnedShutdown* owner;
    std::promise<void>* entered;
    std::shared_future<void> release;
    bool active = false;
    BlockingDestructor(sco::OwnedShutdown* o, std::promise<void>* e, std::shared_future<void> r)
        : owner(o), entered(e), release(std::move(r)) {}
    BlockingDestructor(const BlockingDestructor&) = delete;
    BlockingDestructor(BlockingDestructor&& other)
        : owner(other.owner), entered(other.entered), release(other.release), active(true) {
        owner->fault(); // cancel the reserved start before thread creation
    }
    ~BlockingDestructor() { if (active) { entered->set_value(); release.wait(); } }
    void operator()(const std::function<bool()>&) {}
};

struct LastReferenceDestructor {
    std::promise<void>* entered;
    std::shared_future<void> release;
    bool active = false;
    LastReferenceDestructor(std::promise<void>* e, std::shared_future<void> r)
        : entered(e), release(std::move(r)) {}
    LastReferenceDestructor(const LastReferenceDestructor&) = delete;
    LastReferenceDestructor(LastReferenceDestructor&& other)
        : entered(other.entered), release(other.release), active(true) {}
    ~LastReferenceDestructor() { if (active) { entered->set_value(); release.wait(); } }
    void operator()(const std::function<bool()>&) {}
};

struct DrainFromMove {
    sco::OwnedShutdown* owner;
    bool* refused;
    DrainFromMove(sco::OwnedShutdown* o, bool* r) : owner(o), refused(r) {}
    DrainFromMove(const DrainFromMove&) = delete;
    DrainFromMove(DrainFromMove&& other) : owner(other.owner), refused(other.refused) {
        const auto start = std::chrono::steady_clock::now();
        *refused = !owner->drain_until(start + 5s) && std::chrono::steady_clock::now() - start < 1s;
    }
    void operator()(const std::function<bool()>&) {}
};

struct DrainFromDestructor {
    sco::OwnedShutdown* owner;
    std::promise<void>* entered;
    std::shared_future<void> release;
    std::promise<bool>* result;
    bool active = false;
    DrainFromDestructor(sco::OwnedShutdown* o, std::promise<void>* e, std::shared_future<void> r, std::promise<bool>* v)
        : owner(o), entered(e), release(std::move(r)), result(v) {}
    DrainFromDestructor(const DrainFromDestructor&) = delete;
    DrainFromDestructor(DrainFromDestructor&& other)
        : owner(other.owner), entered(other.entered), release(other.release), result(other.result), active(true) {}
    ~DrainFromDestructor() {
        if (active) {
            const auto start = std::chrono::steady_clock::now();
            result->set_value(!owner->drain_until(start + 5s) && std::chrono::steady_clock::now() - start < 1s);
        }
    }
    void operator()(const std::function<bool()>&) { entered->set_value(); release.wait(); }
};

int main() {
    {
        sco::OwnedShutdown pending;
        std::promise<void> moving, resume;
        auto released = resume.get_future().share();
        bool started = true;
        std::thread starter([&] { started = pending.start_worker(BlockingMove(&moving, released)); });
        moving.get_future().wait();
        const auto begin = std::chrono::steady_clock::now();
        const bool drained = pending.drain_until(begin + 20ms);
        const auto elapsed = std::chrono::steady_clock::now() - begin;
        resume.set_value();
        starter.join();
        CHECK(!drained && !started && !pending.ready());
        // Generous scheduling margin, while catching the original unbounded
        // mutex hold; the deterministic reservation assertion is above.
        CHECK(elapsed < 1s);
    }
    {
        sco::OwnedShutdown reentrant;
        bool entered = false;
        CHECK(reentrant.start_worker(ReentrantMove(&reentrant, &entered)));
        CHECK(entered && reentrant.drain_until(std::chrono::steady_clock::now() + 1s));
    }
    {
        sco::OwnedShutdown pending;
        std::promise<void> destructing, resume;
        auto released = resume.get_future().share();
        bool started = true;
        std::thread starter([&] { started = pending.start_worker(BlockingDestructor(&pending, &destructing, released)); });
        destructing.get_future().wait();
        std::promise<void> drainStarted;
        auto draining = std::async(std::launch::async, [&] {
            drainStarted.set_value();
            return pending.drain_until(std::chrono::steady_clock::now() + 1s);
        });
        drainStarted.get_future().wait();
        CHECK(draining.wait_for(50ms) == std::future_status::timeout);
        CHECK(!pending.ready());
        resume.set_value();
        starter.join();
        CHECK(!draining.get());
        CHECK(!started && !pending.ready());
    }
    {
        sco::OwnedShutdown failed;
        CHECK(!failed.start_worker(ThrowingMove{}));
        CHECK(!failed.drain_until(std::chrono::steady_clock::now() + 1s));
        CHECK(!failed.ready() && !failed.try_read());
    }
    {
        sco::OwnedShutdown pending;
        std::promise<void> destructing, resume;
        auto released = resume.get_future().share();
        bool started = false;
        std::thread starter([&] {
            started = pending.start_worker_with_launch_hook(LastReferenceDestructor(&destructing, released),
                [](std::thread& worker) noexcept { worker.join(); });
        });
        destructing.get_future().wait(); // worker joined, starter holds last reference
        auto draining = std::async(std::launch::async, [&] {
            return pending.drain_until(std::chrono::steady_clock::now() + 1s);
        });
        CHECK(draining.wait_for(50ms) == std::future_status::timeout);
        resume.set_value();
        starter.join();
        CHECK(started && draining.get() && pending.ready());
    }
    {
        sco::OwnedShutdown owner;
        auto read = owner.try_read();
        CHECK(read);
        const auto start = std::chrono::steady_clock::now();
        CHECK(!owner.drain_until(start + 5s));
        CHECK(std::chrono::steady_clock::now() - start < 1s && !owner.ready());
    }
    {
        sco::OwnedShutdown owner;
        bool refused = false;
        CHECK(!owner.start_worker(DrainFromMove(&owner, &refused)));
        CHECK(refused && !owner.ready());
    }
    {
        sco::OwnedShutdown owner;
        std::promise<bool> result;
        CHECK(owner.start_worker([&](const auto&) {
            const auto start = std::chrono::steady_clock::now();
            result.set_value(!owner.drain_until(start + 5s) && std::chrono::steady_clock::now() - start < 1s);
        }));
        CHECK(result.get_future().get() && !owner.ready());
    }
    {
        sco::OwnedShutdown owner;
        std::promise<void> entered, resume;
        std::promise<bool> result;
        CHECK(owner.start_worker(DrainFromDestructor(&owner, &entered, resume.get_future().share(), &result)));
        entered.get_future().wait();
        resume.set_value();
        CHECK(result.get_future().get() && !owner.ready());
    }
    {
        sco::OwnedShutdown owner;
        auto read = owner.try_read();
        CHECK(read);
        std::thread transfer([lease = std::move(read)] { (void)lease; });
        transfer.join();
        CHECK(!owner.open() && !owner.ready());
    }
    {
        sco::OwnedShutdown owner;
        {
            auto read = owner.try_read();
            CHECK(read && !sco::OwnedShutdownHostAccess::begin_startup(owner) && !sco::OwnedShutdownHostAccess::transition(owner));
        }
        CHECK(sco::OwnedShutdownHostAccess::begin_startup(owner));
        CHECK(!owner.try_read() && !owner.start_worker([](const auto&) {}));
        std::atomic<int> phase{0};
        {
            auto transition = sco::OwnedShutdownHostAccess::transition(owner);
            CHECK(transition && !owner.try_read() && !owner.start_worker([](const auto&) {}));
            CHECK(transition.publish_and_open(phase, 0, 1));
            CHECK(!owner.try_read()); // still closed until transition leaves
        }
        CHECK(owner.try_read() && phase == 1);
    }
    {
        sco::OwnedShutdown owner;
        std::atomic<int> phase{0};
        auto transition = sco::OwnedShutdownHostAccess::transition(owner);
        CHECK(transition);
        std::thread fault([&] { owner.fault(); });
        fault.join();
        CHECK(!transition.publish(phase, 0, 1));
        CHECK(phase == 0 && !owner.open());
    }
    {
        sco::OwnedShutdown owner;
        auto transition = sco::OwnedShutdownHostAccess::transition(owner);
        CHECK(transition);
        const auto start = std::chrono::steady_clock::now();
        CHECK(!owner.drain_until(start + 5s));
        CHECK(std::chrono::steady_clock::now() - start < 1s);
        CHECK(!owner.open());
    }
    {
        sco::OwnedShutdown owner;
        std::atomic<int> phase{0};
        std::future<bool> draining;
        {
            auto transition = sco::OwnedShutdownHostAccess::transition(owner);
            CHECK(transition);
            draining = std::async(std::launch::async, [&] {
                return owner.drain_until(std::chrono::steady_clock::now() + 1s);
            });
            CHECK(owner.wait_until_closed(std::chrono::steady_clock::now() + 1s));
            CHECK(!transition.publish(phase, 0, 1));
            CHECK(draining.wait_for(50ms) == std::future_status::timeout);
        }
        CHECK(draining.get() && phase == 0);
    }
    sco::OwnedShutdown domain;
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    std::atomic<int> cachedCalls{0};
    CHECK(domain.start_worker([&](const auto& stop) {
        auto read = domain.try_read();
        CHECK(read);
        entered.set_value();
        released.wait();
        ++cachedCalls; // synthetic cached-table read, still inside admitted lifetime
        CHECK(stop());
    }));
    entered.get_future().wait();
    CHECK(!domain.drain_until(std::chrono::steady_clock::now() + 1ms));
    CHECK(!domain.ready());
    CHECK(!domain.try_read());
    CHECK(!domain.start_worker([](const auto&) {}));
    release.set_value();
    while (cachedCalls.load() != 1) std::this_thread::yield();
    // A failed drain is terminal: joining later cannot manufacture readiness.
    CHECK(!domain.drain_until(std::chrono::steady_clock::now() + 1s));
    CHECK(cachedCalls == 1);
    sco::OwnedShutdown good;
    std::promise<void> started;
    CHECK(good.start_worker([&](const auto& stop) {
        started.set_value();
        while (!stop()) std::this_thread::yield();
    }));
    started.get_future().wait();
    CHECK(good.drain_until(std::chrono::steady_clock::now() + 1s));
    CHECK(good.ready() && !good.try_read());
    CHECK(!good.start_worker([](const auto&) {}));
    sco::OwnedShutdown throwing;
    CHECK(throwing.start_worker([](const auto&) { throw std::runtime_error("worker fault"); }));
    CHECK(!throwing.drain_until(std::chrono::steady_clock::now() + 1s));
    CHECK(!throwing.ready());
    sco::OwnedShutdown invalidated;
    CHECK(invalidated.drain_until(std::chrono::steady_clock::now() + 1s));
    invalidated.fault();
    CHECK(!invalidated.ready());
    // A worker's body return is not join: thread-local destruction can block.
    std::promise<void> tlsEntered, tlsRelease;
    auto tlsReleased = tlsRelease.get_future().share();
    struct Tail {
        std::promise<void>* entered;
        std::shared_future<void> release;
        ~Tail() { entered->set_value(); release.wait(); }
    };
    sco::OwnedShutdown tls;
    CHECK(tls.start_worker([&](const auto&) {
        thread_local Tail tail{&tlsEntered, tlsReleased};
        (void)tail;
    }));
    tlsEntered.get_future().wait();
    CHECK(!tls.drain_until(std::chrono::steady_clock::now() + 1ms));
    CHECK(!tls.ready());
    tlsRelease.set_value();
}
