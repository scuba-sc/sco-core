// SPDX-License-Identifier: GPL-3.0-only
#define SCO_OWNED_SHUTDOWN_TESTING
#include "sco/owned_shutdown.h"
#include "../src/owned_shutdown_access.h"
#include "owned_shutdown_test_access.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); std::abort(); } } while (0)
#include <chrono>
#include <future>
#include <optional>
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

// Deferred execution: these cases exercise the close/request boundary using
// actual leases and worker-start reservations, not a success-only drain stub.
static void TestStopRequest() {
    {
        sco::OwnedShutdown owner;
        CHECK(owner.drain_state() == sco::OwnedShutdown::DrainState::Open);
        {
            auto read = owner.try_read(); CHECK(read);
            owner.request_stop(); // must return while this same-thread read exists
            owner.request_stop();
            CHECK(owner.closed() && !owner.open() && !owner.ready());
            CHECK(owner.drain_state() == sco::OwnedShutdown::DrainState::Requested);
            CHECK(!owner.try_read() && !owner.start_worker([](const auto&) {}));
        }
        CHECK(owner.drain_until(std::chrono::steady_clock::now() + 1s));
        CHECK(owner.drain_state() == sco::OwnedShutdown::DrainState::Drained);
        owner.request_stop();
        CHECK(owner.ready()); // a repeated request cannot revoke or invent proof
        owner.fault();
        owner.request_stop();
        CHECK(!owner.ready()); // it cannot clear a later fault either
        CHECK(owner.drain_state() == sco::OwnedShutdown::DrainState::Retained);
    }
    {
        sco::OwnedShutdown owner;
        std::promise<void> requested;
        std::atomic<bool> tlsDestroyed{false};
        struct Tail {
            std::atomic<bool>* destroyed;
            ~Tail() { destroyed->store(true); }
        };
        CHECK(owner.start_worker([&](const auto& stop) {
            thread_local Tail tail{&tlsDestroyed}; (void)tail;
            owner.request_stop(); // a worker can request without self-draining
            CHECK(stop() && !owner.try_read());
            requested.set_value();
        }));
        CHECK(requested.get_future().wait_for(1s) == std::future_status::ready);
        CHECK(owner.drain_state() == sco::OwnedShutdown::DrainState::Requested);
        CHECK(owner.drain_until(std::chrono::steady_clock::now() + 1s));
        CHECK(owner.ready() && tlsDestroyed.load());
        CHECK(owner.drain_state() == sco::OwnedShutdown::DrainState::Drained);
        CHECK(sco::OwnedShutdownTestAccess::handles_reaped(owner));
    }
    {
        sco::OwnedShutdown owner;
        std::promise<void> constructing, release;
        auto released = release.get_future().share();
        auto start = std::async(std::launch::async, [&] {
            return owner.start_worker(BlockingMove(&constructing, released));
        });
        CHECK(constructing.get_future().wait_for(1s) == std::future_status::ready);
        owner.request_stop(); // callable construction is outside the owner mutex
        CHECK(owner.closed() && !owner.ready());
        release.set_value();
        CHECK(!start.get()); // reserved construction cancels before body launch
        CHECK(owner.drain_until(std::chrono::steady_clock::now() + 1s));
    }
    {
        sco::OwnedShutdown owner;
        CHECK(sco::OwnedShutdownHostAccess::begin_startup(owner));
        std::atomic<int> phase{0};
        {
            auto transition = sco::OwnedShutdownHostAccess::transition(owner);
            CHECK(transition);
            owner.request_stop();
            CHECK(!transition.valid_for(owner));
            CHECK(!transition.publish_and_open(phase, 0, 1));
        }
        CHECK(phase.load() == 0 && !owner.try_read());
        CHECK(owner.drain_until(std::chrono::steady_clock::now() + 1s));
    }
    {
        sco::OwnedShutdown owner;
        std::promise<void> entered, release;
        auto released = release.get_future().share();
        std::thread reader([&] {
            auto read = owner.try_read(); CHECK(read);
            entered.set_value(); released.wait();
        });
        CHECK(entered.get_future().wait_for(1s) == std::future_status::ready);
        owner.request_stop();
        CHECK(!owner.drain_until(std::chrono::steady_clock::now() + 1ms));
        release.set_value(); reader.join();
        owner.request_stop();
        CHECK(owner.reap_retained_until(std::chrono::steady_clock::now() + 1s) ==
              sco::OwnedShutdown::ReapResult::ReapedRetained);
        CHECK(!owner.ready() && !owner.try_read());
    }
}

static void TestAbandonedReaders() {
    using Reap = sco::OwnedShutdown::ReapResult;
    {
        sco::OwnedShutdown owner;
        auto read = owner.try_read(); CHECK(read);
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        auto draining = std::async(std::launch::async, [&] { return owner.drain_until(deadline); });
        CHECK(owner.wait_until_closed(std::chrono::steady_clock::now() + 1s));
        CHECK(draining.wait_for(0ms) == std::future_status::timeout);
        std::thread foreign([lease = std::move(read)] { (void)lease; });
        foreign.join();
        CHECK(draining.wait_for(1s) == std::future_status::ready);
        CHECK(std::chrono::steady_clock::now() < deadline && !draining.get());
        CHECK(owner.closed() && !owner.ready());
        CHECK(owner.reap_retained_until(std::chrono::steady_clock::now() + 1s) == Reap::Refused);
        auto other = std::async(std::launch::async, [&] {
            return owner.reap_retained_until(std::chrono::steady_clock::now() + 1s);
        });
        CHECK(other.get() == Reap::ReapedRetained && !owner.ready());
    }
    {
        // Keep both origin threads alive and acquire every lease before the
        // first foreign release closes admission. Each has a second active
        // lease: releasing it must not erase the abandoned origin record.
        sco::OwnedShutdown owner;
        auto first = owner.try_read(); CHECK(first);
        std::optional<sco::OwnedShutdown::Read> nested;
        nested.emplace(owner.try_read()); CHECK(*nested);
        std::promise<std::unique_ptr<sco::OwnedShutdown::Read>> otherLease;
        auto otherLeaseFuture = otherLease.get_future();
        std::promise<void> releaseNested, nestedReleased, testOrigin, finishOrigin;
        std::promise<Reap> originResult;
        auto nestedReleasedFuture = nestedReleased.get_future();
        auto originResultFuture = originResult.get_future();
        std::thread otherOrigin([&] {
            auto firstOther = owner.try_read(); CHECK(firstOther);
            std::optional<sco::OwnedShutdown::Read> otherNested;
            otherNested.emplace(owner.try_read()); CHECK(*otherNested);
            otherLease.set_value(std::make_unique<sco::OwnedShutdown::Read>(std::move(firstOther)));
            releaseNested.get_future().wait();
            otherNested.reset();
            nestedReleased.set_value();
            testOrigin.get_future().wait();
            originResult.set_value(owner.reap_retained_until(std::chrono::steady_clock::now() + 1s));
            finishOrigin.get_future().wait();
        });
        CHECK(otherLeaseFuture.wait_for(1s) == std::future_status::ready);
        auto otherRead = otherLeaseFuture.get();
        std::thread foreign([one = std::move(first), two = std::move(otherRead)] { (void)one; (void)two; });
        foreign.join();
        nested.reset();
        releaseNested.set_value();
        CHECK(nestedReleasedFuture.wait_for(1s) == std::future_status::ready);
        auto draining = std::async(std::launch::async, [&] {
            return owner.drain_until(std::chrono::steady_clock::now() + 2s);
        });
        CHECK(draining.wait_for(1s) == std::future_status::ready && !draining.get());
        CHECK(!owner.ready());
        CHECK(owner.reap_retained_until(std::chrono::steady_clock::now() + 1s) == Reap::Refused);
        testOrigin.set_value();
        CHECK(originResultFuture.wait_for(1s) == std::future_status::ready);
        CHECK(originResultFuture.get() == Reap::Refused);
        auto control = std::async(std::launch::async, [&] {
            return owner.reap_retained_until(std::chrono::steady_clock::now() + 1s);
        });
        CHECK(control.get() == Reap::ReapedRetained && !owner.ready());
        finishOrigin.set_value();
        otherOrigin.join();
    }
    {
        sco::OwnedShutdown owner;
        { auto first = owner.try_read(); auto moved = std::move(first); CHECK(!first && moved); }
        CHECK(owner.drain_until(std::chrono::steady_clock::now() + 1s) && owner.ready());
    }
}

int main() {
    TestAbandonedReaders();
    TestStopRequest();
    using Reap = sco::OwnedShutdown::ReapResult;
    {
        sco::OwnedShutdown fresh;
        CHECK(fresh.reap_retained_until(std::chrono::steady_clock::now() + 1s) == Reap::Refused);
        CHECK(fresh.open() && fresh.try_read());
    }
    {
        // Timeout before body completion creates no helper. Later maintenance
        // times out inside TLS join, then retries that SAME retained helper.
        sco::OwnedShutdown retained;
        std::promise<void> bodyEntered, bodyRelease, tlsEntered, tlsRelease;
        auto bodyGo = bodyRelease.get_future().share();
        auto tlsGo = tlsRelease.get_future().share();
        std::atomic<bool> tlsDestroyed{false};
        struct Tail {
            std::promise<void>* entered;
            std::shared_future<void> release;
            std::atomic<bool>* destroyed;
            ~Tail() { entered->set_value(); release.wait(); destroyed->store(true); }
        };
        CHECK(retained.start_worker([&](const auto&) {
            thread_local Tail tail{&tlsEntered, tlsGo, &tlsDestroyed};
            (void)tail;
            bodyEntered.set_value(); bodyGo.wait();
        }));
        bodyEntered.get_future().wait();
        CHECK(!retained.drain_until(std::chrono::steady_clock::now() + 1ms));
        CHECK(!sco::OwnedShutdownTestAccess::helper_owned(retained));
        CHECK(retained.reap_retained_until(std::chrono::steady_clock::now() + 1ms) == Reap::PendingRetained);
        auto maintenance = std::async(std::launch::async, [&] {
            return retained.reap_retained_until(std::chrono::steady_clock::now() + 2s);
        });
        CHECK(sco::OwnedShutdownTestAccess::wait_for_join_control(retained, std::chrono::steady_clock::now() + 1s));
        CHECK(retained.reap_retained_until(std::chrono::steady_clock::now() + 1s) == Reap::Refused);
        CHECK(!retained.drain_until(std::chrono::steady_clock::now() + 1s));
        bodyRelease.set_value();
        tlsEntered.get_future().wait();
        CHECK(sco::OwnedShutdownTestAccess::wait_for_helper(retained, std::chrono::steady_clock::now() + 1s));
        CHECK(!tlsDestroyed.load() && !retained.ready());
        CHECK(!sco::OwnedShutdownTestAccess::handles_reaped(retained));
        CHECK(maintenance.get() == Reap::PendingRetained);
        CHECK(sco::OwnedShutdownTestAccess::helper_owned(retained));
        tlsRelease.set_value();
        CHECK(retained.reap_retained_until(std::chrono::steady_clock::now() + 2s) == Reap::ReapedRetained);
        CHECK(tlsDestroyed.load() && sco::OwnedShutdownTestAccess::handles_reaped(retained));
        CHECK(!retained.ready() && retained.closed() && !retained.try_read());
        CHECK(!retained.start_worker([](const auto&) {}));
        CHECK(!retained.drain_until(std::chrono::steady_clock::now() + 1s));
        CHECK(retained.reap_retained_until(std::chrono::steady_clock::now() + 1s) == Reap::ReapedRetained);
    }
    {
        // Maintenance refuses a worker/TLS thread instead of self-joining.
        sco::OwnedShutdown self;
        std::promise<bool> refused;
        CHECK(self.start_worker([&](const auto&) {
            const bool failed = !self.drain_until(std::chrono::steady_clock::now() + 2s);
            refused.set_value(failed && self.reap_retained_until(std::chrono::steady_clock::now() + 2s) == Reap::Refused);
        }));
        CHECK(refused.get_future().get());
        CHECK(self.reap_retained_until(std::chrono::steady_clock::now() + 2s) == Reap::ReapedRetained);
        CHECK(!self.ready() && sco::OwnedShutdownTestAccess::handles_reaped(self));
    }
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
        // A same-thread move preserves the acquiring thread and normal release.
        sco::OwnedShutdown owner;
        {
            auto first = sco::OwnedShutdownHostAccess::transition(owner);
            auto moved = std::move(first);
            CHECK(!first && moved && !owner.open());
        }
        CHECK(owner.open());
        CHECK(owner.drain_until(std::chrono::steady_clock::now() + 1s));
        CHECK(owner.ready());
    }
    {
        // A waiting drain must not turn a foreign release into readiness.
        sco::OwnedShutdown owner;
        auto transition = sco::OwnedShutdownHostAccess::transition(owner);
        CHECK(transition);
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        auto draining = std::async(std::launch::async, [&] {
            return owner.drain_until(deadline);
        });
        CHECK(owner.wait_until_closed(std::chrono::steady_clock::now() + 1s));
        CHECK(draining.wait_for(0ms) == std::future_status::timeout);
        std::thread transfer([lease = std::move(transition)] { (void)lease; });
        transfer.join();
        CHECK(draining.wait_for(1s) == std::future_status::ready);
        CHECK(std::chrono::steady_clock::now() < deadline);
        CHECK(!draining.get());
        CHECK(owner.closed() && !owner.ready());
        CHECK(owner.drain_state() == sco::OwnedShutdown::DrainState::Retained);
        // The acquiring thread still cannot join its own obligation.
        CHECK(owner.reap_retained_until(std::chrono::steady_clock::now() + 1s) ==
              sco::OwnedShutdown::ReapResult::Refused);
        auto maintenance = std::async(std::launch::async, [&] {
            return owner.reap_retained_until(std::chrono::steady_clock::now() + 1s);
        });
        CHECK(maintenance.get() == sco::OwnedShutdown::ReapResult::ReapedRetained);
        CHECK(!owner.ready());
    }
    {
        // Destruction on a different thread invalidates the ownership proof.
        sco::OwnedShutdown owner;
        auto transition = sco::OwnedShutdownHostAccess::transition(owner);
        CHECK(transition);
        std::thread transfer([lease = std::move(transition)] { (void)lease; });
        transfer.join();
        CHECK(owner.closed() && !owner.open() && !owner.ready());
        CHECK(owner.drain_state() == sco::OwnedShutdown::DrainState::Retained);
        CHECK(!owner.try_read() && !owner.start_worker([](const auto&) {}));
        CHECK(!owner.drain_until(std::chrono::steady_clock::now() + 20ms));
        CHECK(!owner.ready());
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
    CHECK(sco::OwnedShutdownTestAccess::handles_reaped(good));
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
    CHECK(sco::OwnedShutdownTestAccess::helper_owned(tls));
    tlsRelease.set_value();
    CHECK(tls.reap_retained_until(std::chrono::steady_clock::now() + 2s) == Reap::ReapedRetained);
    CHECK(sco::OwnedShutdownTestAccess::handles_reaped(tls) && !tls.ready());
}
