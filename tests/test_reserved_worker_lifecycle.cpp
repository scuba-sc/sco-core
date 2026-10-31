// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "../src/reserved_worker_lifecycle.h"
#include <atomic>
#include <chrono>
#include <new>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
using namespace std::chrono_literals;
using Group = sco::ReservedWorkerLifecycle;
using Result = Group::Result;
using Clock = Group::Clock;
struct Capsule { std::shared_ptr<int> backing; };

// Allocation hooks re-enter the lifecycle. If an allocation is performed
// while its mutex is held, this test deadlocks (the runner has a timeout).
static std::atomic<Group*> allocationGroup{nullptr};
static std::atomic<int> reentries{0};
static thread_local bool inAllocation = false;
void* operator new(std::size_t n) {
    if (!inAllocation) {
        inAllocation = true;
        if (auto* group = allocationGroup.load(std::memory_order_acquire)) {
            (void)group->wait_until_closed(Clock::now());
            ++reentries;
        }
        inAllocation = false;
    }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

struct Gate {
    std::promise<void> entered;
    std::promise<void> release;
    std::shared_future<void> proceed = release.get_future().share();
    void wait() { entered.set_value(); proceed.wait(); }
};

int main() {
    {
        Group group(std::make_unique<int>(0));
        allocationGroup.store(&group, std::memory_order_release);
        { auto start = group.reserve_start(); CHECK(start); CHECK(reentries.load() == 0);
          CHECK(start.launch([](const auto&) {})); }
        CHECK(group.retire_until(Clock::now() + 5s) == Result::Joined);
        allocationGroup.store(nullptr, std::memory_order_release);
        CHECK(reentries.load() > 0);
    }
    // Retirement cannot pass a reserved but unpublished worker, including
    // callable construction and temporaries on the starter thread.
    {
        auto backing = std::make_shared<int>(1);
        std::weak_ptr<int> weak = backing;
        Group group(std::make_unique<Capsule>(backing));
        backing.reset();
        Gate construction, body;
        std::thread starter([&] {
            auto start = group.reserve_start();
            CHECK(start);
            construction.wait();
            CHECK(start.launch([&](const auto&) { body.wait(); }));
        });
        construction.entered.get_future().wait();
        auto retired = std::async(std::launch::async, [&] { return group.retire_until(Clock::now() + 5s); });
        // Observe closure without relying on scheduling delays.
        CHECK(group.wait_until_closed(Clock::now() + 5s));
        CHECK(retired.wait_for(0s) == std::future_status::timeout);
        construction.release.set_value();
        body.entered.get_future().wait();
        starter.join();
        CHECK(retired.wait_for(0s) == std::future_status::timeout);
        body.release.set_value();
        CHECK(retired.get() == Result::Joined);
        CHECK(!weak.expired());
    }
    // The thread's TLS destructor must complete before join is acknowledged.
    {
        Group group(std::make_unique<int>(2));
        Gate tail;
        struct Tail { Gate* gate; ~Tail() { gate->wait(); } };
        {
            auto start = group.reserve_start(); CHECK(start);
            CHECK(start.launch([&](const auto&) { thread_local Tail value{&tail}; (void)value; }));
        }
        tail.entered.get_future().wait();
        auto retired = std::async(std::launch::async, [&] { return group.retire_until(Clock::now() + 5s); });
        CHECK(group.wait_until_closed(Clock::now() + 5s));
        CHECK(retired.wait_for(0s) == std::future_status::timeout);
        tail.release.set_value();
        CHECK(retired.get() == Result::Joined);
    }
    // The stored callable's captured objects also finish destruction before join.
    {
        Group group(std::make_unique<int>(7));
        Gate destruction;
        struct Captured { Gate* gate; ~Captured() { gate->wait(); } };
        {
            auto start = group.reserve_start(); CHECK(start);
            auto captured = std::make_unique<Captured>(&destruction);
            CHECK(start.launch([owned = std::move(captured)](const auto&) { (void)owned; }));
        }
        destruction.entered.get_future().wait();
        auto retired = std::async(std::launch::async, [&] { return group.retire_until(Clock::now() + 5s); });
        CHECK(group.wait_until_closed(Clock::now() + 5s));
        CHECK(retired.wait_for(0s) == std::future_status::timeout);
        destruction.release.set_value();
        CHECK(retired.get() == Result::Joined);
    }
    // An abandoned reservation, including construction unwinding, faults it.
    {
        Group group(std::make_unique<int>(8));
        try { auto start = group.reserve_start(); CHECK(start); throw std::runtime_error("construction"); }
        catch (const std::runtime_error&) {}
        CHECK(group.retire_until(Clock::now() + 5s) == Result::FailedRetained);
    }
    // Self-join is refused without closing, so a different thread can retire.
    {
        Group group(std::make_unique<int>(3));
        std::promise<Result> self;
        auto selfResult = self.get_future();
        {
            auto start = group.reserve_start(); CHECK(start);
            CHECK(start.launch([&](const auto&) { self.set_value(group.retire_until(Clock::now() + 5s)); }));
            // The worker must be identified even while its start reservation
            // and handle-publication barrier are still held by this thread.
            CHECK(selfResult.get() == Result::SelfJoinRefused);
        }
        CHECK(group.retire_until(Clock::now() + 5s) == Result::Joined);
    }
    // Failure and timeout retain backing even after wrapper destruction.
    {
        auto backing = std::make_shared<int>(4);
        std::weak_ptr<int> weak = backing;
        // The timed-out worker may outlive this scope, so its callable owns
        // the test context until it has finished using it.
        auto body = std::make_shared<Gate>();
        auto bodyDone = std::make_shared<std::promise<void>>();
        {
            Group group(std::make_unique<Capsule>(backing)); backing.reset();
            { auto start = group.reserve_start(); CHECK(start); CHECK(start.launch([body, bodyDone](const auto&) { body->wait(); bodyDone->set_value(); })); }
            body->entered.get_future().wait();
            CHECK(group.retire_until(Clock::now() + 20ms) == Result::FailedRetained);
        }
        CHECK(!weak.expired());
        body->release.set_value();
        bodyDone->get_future().wait();
        CHECK(!weak.expired());
    }
    {
        Group group(std::make_unique<int>(5));
        { auto start = group.reserve_start(); CHECK(start); CHECK(start.launch([](const auto&) { throw std::runtime_error("worker"); })); }
        CHECK(group.retire_until(Clock::now() + 5s) == Result::FailedRetained);
    }
    {
        Group group(std::make_unique<int>(6));
        { auto start = group.reserve_start(); CHECK(start); CHECK(group.retire_until(Clock::now() + 5s) == Result::SelfJoinRefused); }
        CHECK(group.retire_until(Clock::now() + 5s) == Result::FailedRetained);
    }
}
