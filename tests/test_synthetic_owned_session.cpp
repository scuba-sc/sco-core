// SPDX-License-Identifier: GPL-3.0-only
#include "../src/synthetic_owned_session.h"
#include "sco/signature_registry.h"
#include "issue302_test_support.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); std::abort(); } } while (0)
using namespace std::chrono_literals;
using Session = sco::SyntheticOwnedSession;
using Result = Session::RetireResult;
using Status = Session::Status;
using Clock = std::chrono::steady_clock;

struct CommitGate {
    std::promise<void> entered;
    std::shared_future<void> release;
    static void Wait(void* context) {
        auto& gate = *static_cast<CommitGate*>(context);
        gate.entered.set_value();
        gate.release.wait();
    }
};
static void WaitAndThrow(void* context) {
    CommitGate::Wait(context);
    throw std::runtime_error("synthetic table call failure");
}
static void WaitPastDeadline(void* context) {
    const auto deadline = *static_cast<Clock::time_point*>(context);
    std::this_thread::sleep_until(deadline + 20ms);
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "success-only") {
        Session session({{"success", 1}});
        std::promise<void> started;
        CHECK(session.owner_for_test().start_worker([&](const auto& stop) {
            started.set_value();
            while (!stop()) std::this_thread::yield();
        }));
        started.get_future().wait();
        CHECK(session.retire_until(Clock::now() + 3s) == Result::QuiescedRetained);
        CHECK(session.status() == Status::QuiescedRetained && !session.backing_for_test().expired());
        return 0;
    }
    CHECK(!sco::AcquireHostSignatureRegistry());
    {
        bool refused = false;
        try { Session invalid({{"same", 1}, {"same", 2}}); }
        catch (const std::invalid_argument&) { refused = true; }
        CHECK(refused);
        refused = false;
        try { Session invalid({{std::string("a\0b", 3), 1}}); }
        catch (const std::invalid_argument&) { refused = true; }
        CHECK(refused);
        refused = false;
        try { Session invalid({{std::string(Session::MaxKeyBytes + 1, 'a'), 1}}); }
        catch (const std::invalid_argument&) { refused = true; }
        CHECK(refused);
    }
    std::weak_ptr<const void> successfulBacking;
    {
        Session session({{"beta", 2}, {"alpha", 1}});
        successfulBacking = session.backing_for_test();
        auto missing = session.try_acquire();
        CHECK(missing);
        uint64_t value = 9;
        CHECK(missing.lookup("alpha", value) && value == 1);
        CHECK(!missing.lookup("absent", value) && value == 1);
        // Drop this read on its acquiring thread before testing the other one.
    }
    CHECK(!successfulBacking.expired());
    std::weak_ptr<const void> retiredBacking;
    {
        Session session({{"first", 11}});
        retiredBacking = session.backing_for_test();
        std::promise<void> entered, release;
        auto proceed = release.get_future().share();
        std::thread reader([&] {
            auto read = session.try_acquire();
            CHECK(read);
            uint64_t value = 0;
            CHECK(read.lookup("first", value) && value == 11);
            entered.set_value();
            proceed.wait();
        });
        entered.get_future().wait();
        auto retirement = std::async(std::launch::async, [&] { return session.retire_until(Clock::now() + 3s); });
        CHECK(session.owner_for_test().wait_until_closed(Clock::now() + 2s));
        CHECK(session.status() == Status::Quiescing);
        CHECK(!session.try_acquire());
        CHECK(!session.owner_for_test().start_worker([](const auto&) {}));
        CHECK(session.retire_until(Clock::now() + 1s) == Result::Unavailable);
        CHECK(retirement.wait_for(50ms) == std::future_status::timeout);
        release.set_value();
        reader.join();
        CHECK(retirement.get() == Result::QuiescedRetained);
        CHECK(session.status() == Status::QuiescedRetained);
        CHECK(!session.try_acquire());
        CHECK(session.retire_until(Clock::now() + 1s) == Result::Unavailable);
        CHECK(!session.backing_for_test().expired());
    }
    CHECK(!retiredBacking.expired());
    {
        auto session = std::make_unique<Session>(std::vector<Session::Entry>{{"cached", 42}});
        const auto copied = session->table();
        uint64_t unchanged = 77;
        const std::string oversized(Session::MaxKeyBytes + 1, 'x');
        CHECK(!copied.lookup(copied.context, oversized.c_str(), &unchanged) && unchanged == 77);
        auto weak = session->backing_for_test();
        std::promise<void> release;
        CommitGate gate{{}, release.get_future().share()};
        session->set_table_hook_for_test(CommitGate::Wait, &gate);
        uint64_t output = 0;
        auto call = std::async(std::launch::async, [&] { return copied.lookup(copied.context, "cached", &output); });
        gate.entered.get_future().wait();
        auto retirement = std::async(std::launch::async, [&] { return session->retire_until(Clock::now() + 3s); });
        CHECK(session->owner_for_test().wait_until_closed(Clock::now() + 2s));
        uint64_t refused = 99;
        CHECK(!copied.lookup(copied.context, "cached", &refused) && refused == 99);
        CHECK(retirement.wait_for(50ms) == std::future_status::timeout);
        release.set_value();
        CHECK(call.get() && output == 42);
        CHECK(retirement.get() == Result::QuiescedRetained);
        session.reset();
        CHECK(!weak.expired());
        CHECK(!copied.lookup(copied.context, "cached", &refused) && refused == 99);
        Session newer({{"cached", 88}});
        const auto next = newer.table();
        CHECK(next.lookup(next.context, "cached", &refused) && refused == 88);
        CHECK(!copied.lookup(copied.context, "cached", &refused) && refused == 88);
    }
    {
        Session session({{"throw-call", 7}});
        const auto copied = session.table();
        std::promise<void> release;
        CommitGate gate{{}, release.get_future().share()};
        session.set_table_hook_for_test(WaitAndThrow, &gate);
        std::promise<void> releaseFault;
        CommitGate faultGate{{}, releaseFault.get_future().share()};
        session.set_table_fault_hook_for_test(CommitGate::Wait, &faultGate);
        uint64_t output = 99;
        auto call = std::async(std::launch::async, [&] { return copied.lookup(copied.context, "throw-call", &output); });
        gate.entered.get_future().wait();
        auto retirement = std::async(std::launch::async, [&] { return session.retire_until(Clock::now() + 3s); });
        CHECK(session.owner_for_test().wait_until_closed(Clock::now() + 2s));
        CHECK(retirement.wait_for(50ms) == std::future_status::timeout);
        release.set_value();
        faultGate.entered.get_future().wait(); // fault published while call still owns its lease
        CHECK(retirement.wait_for(50ms) == std::future_status::timeout);
        releaseFault.set_value();
        CHECK(!call.get() && output == 99);
        CHECK(retirement.get() == Result::FailedRetained);
        CHECK(session.status() == Status::FailedRetained && !session.backing_for_test().expired());
    }
    {
        Session session({{"tls", 1}});
        std::promise<void> tlsEntered, tlsRelease;
        auto released = tlsRelease.get_future().share();
        struct Tail {
            std::promise<void>* entered;
            std::shared_future<void> released;
            ~Tail() { entered->set_value(); released.wait(); }
        };
        CHECK(session.owner_for_test().start_worker([&](const auto&) {
            thread_local Tail tail{&tlsEntered, released};
            (void)tail;
        }));
        tlsEntered.get_future().wait();
        auto retirement = std::async(std::launch::async, [&] { return session.retire_until(Clock::now() + 3s); });
        CHECK(session.owner_for_test().wait_until_closed(Clock::now() + 2s));
        CHECK(retirement.wait_for(50ms) == std::future_status::timeout);
        tlsRelease.set_value();
        CHECK(retirement.get() == Result::QuiescedRetained);
        CHECK(!session.backing_for_test().expired());
    }
    {
        Session session({{"fault", 1}});
        auto weak = session.backing_for_test();
        std::promise<void> release;
        CommitGate gate{{}, release.get_future().share()};
        auto retirement = std::async(std::launch::async, [&] {
            return session.retire_with_hook_for_test(Clock::now() + 3s, CommitGate::Wait, &gate);
        });
        gate.entered.get_future().wait();
        session.owner_for_test().fault();
        release.set_value();
        CHECK(retirement.get() == Result::FailedRetained);
        CHECK(session.status() == Status::FailedRetained && !session.try_acquire());
        CHECK(session.retire_until(Clock::now() + 1s) == Result::Unavailable && !weak.expired());
    }
    {
        Session session({{"commit-first", 1}});
        CHECK(session.retire_until(Clock::now() + 2s) == Result::QuiescedRetained);
        CHECK(session.owner_for_test().ready());
        std::thread laterFault([&] { session.owner_for_test().fault(); });
        laterFault.join();
        CHECK(session.owner_for_test().ready());
        CHECK(session.status() == Status::QuiescedRetained && !session.try_acquire());
        CHECK(session.retire_until(Clock::now() + 1s) == Result::Unavailable);
        CHECK(!session.backing_for_test().expired());
    }
    {
        Session session({{"expired", 1}});
        auto deadline = Clock::now() + 100ms;
        CHECK(session.retire_with_hook_for_test(deadline, WaitPastDeadline, &deadline) == Result::FailedRetained);
        CHECK(session.status() == Status::FailedRetained && !session.backing_for_test().expired());
    }
    {
        Session session({{"throwing-worker", 1}});
        std::promise<void> started;
        CHECK(session.owner_for_test().start_worker([&](const auto&) {
            started.set_value();
            throw std::runtime_error("synthetic worker failure");
        }));
        started.get_future().wait();
        CHECK(session.retire_until(Clock::now() + 2s) == Result::FailedRetained);
        CHECK(session.status() == Status::FailedRetained && !session.backing_for_test().expired());
    }
    {
        Session session({{"thread-bound", 3}});
        auto read = session.try_acquire();
        CHECK(read);
        std::thread invalidTransfer([lease = std::move(read)] {
            uint64_t value = 0;
            CHECK(!lease && !lease.lookup("thread-bound", value));
        });
        invalidTransfer.join();
        CHECK(session.status() == Status::FailedRetained && !session.try_acquire());
        CHECK(session.retire_until(Clock::now() + 1s) == Result::FailedRetained);
    }
    {
        struct Signals { std::promise<void> entered, release, finished; };
        auto signals = std::make_shared<Signals>();
        auto proceed = signals->release.get_future().share();
        auto session = std::make_unique<Session>(std::vector<Session::Entry>{{"worker", 1}});
        auto weak = session->backing_for_test();
        CHECK(session->owner_for_test().start_worker([signals, proceed](const auto&) {
            signals->entered.set_value();
            proceed.wait();
            signals->finished.set_value();
        }));
        signals->entered.get_future().wait();
        CHECK(session->retire_until(Clock::now() + 20ms) == Result::FailedRetained);
        session.reset();
        CHECK(!weak.expired());
        signals->release.set_value();
        signals->finished.get_future().wait();
        CHECK(!weak.expired());
    }
    {
        std::promise<void> entered, release;
        auto proceed = release.get_future().share();
        std::weak_ptr<const void> weak;
        auto session = std::make_unique<Session>(std::vector<Session::Entry>{{"held", 7}});
        weak = session->backing_for_test();
        Session* target = session.get();
        std::thread reader([&] {
            auto read = target->try_acquire();
            CHECK(read);
            entered.set_value();
            proceed.wait();
            uint64_t value = 0;
            CHECK(!read.lookup("held", value));
        });
        entered.get_future().wait();
        CHECK(session->retire_until(Clock::now() + 20ms) == Result::FailedRetained);
        CHECK(session->status() == Status::FailedRetained);
        session.reset();
        CHECK(!weak.expired());
        release.set_value();
        reader.join();
        CHECK(!weak.expired());
    }
    {
        Session session({{"self", 1}});
        auto read = session.try_acquire();
        CHECK(read);
        const auto start = Clock::now();
        CHECK(session.retire_until(start + 3s) == Result::FailedRetained);
        CHECK(Clock::now() - start < 1s);
        CHECK(session.status() == Status::FailedRetained && !session.backing_for_test().expired());
    }
    {
        Session first({{"key", 1}}), second({{"key", 2}});
        auto a = first.try_acquire();
        auto b = second.try_acquire();
        uint64_t x = 0, y = 0;
        CHECK(a.lookup("key", x) && b.lookup("key", y) && x == 1 && y == 2);
    }
    {
        const std::string limit(Session::MaxKeyBytes, 'z');
        Session exact({{limit, 19}});
        const auto table = exact.table();
        uint64_t value = 0;
        CHECK(table.lookup(table.context, limit.c_str(), &value) && value == 19);
        issue302::GuardPages pages;
        char* const bounded = reinterpret_cast<char*>(pages.data + pages.page - Session::MaxKeyBytes - 1);
        std::fill_n(bounded, Session::MaxKeyBytes, 'z');
        bounded[Session::MaxKeyBytes] = '\0';
        CHECK(table.lookup(table.context, bounded, &value) && value == 19);
        bounded[Session::MaxKeyBytes] = 'z';
        CHECK(!table.lookup(table.context, bounded, &value) && value == 19);
    }
    {
        Session empty({});
        auto lease = empty.try_acquire();
        uint64_t value = 99;
        CHECK(lease && !lease.lookup("missing", value) && value == 99);
    }
    CHECK(!sco::AcquireHostSignatureRegistry());
}
