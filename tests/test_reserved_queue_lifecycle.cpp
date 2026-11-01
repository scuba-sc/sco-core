// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "../src/reserved_queue_lifecycle.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <limits>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
using Queue = sco::reserved::detail::OwnedTaskQueue;
using namespace std::chrono_literals;

int main() {
    {
        Queue q;
        uint64_t first = 0, second = 0, third = 0, value = 99, completed = 99;
        CHECK(q.Enqueue(Queue::Operation::Add, 0, &first) == Queue::EnqueueResult::Accepted && first == 1);
        Queue::Claim held; CHECK(q.TryClaim(held));
        CHECK(q.Enqueue(Queue::Operation::Set, 0, &second) == Queue::EnqueueResult::Accepted && second == 2);
        Queue::Claim denied; CHECK(!q.TryClaim(denied));
        CHECK(held.Execute());
        CHECK(q.Enqueue(Queue::Operation::Set, 1, &third) == Queue::EnqueueResult::Accepted && third == 3);
        Queue::Claim next; CHECK(q.TryClaim(next) && next.Execute());
        q.Progress(value, completed); CHECK(value == 0 && completed == second);
        Queue::Claim last; CHECK(q.TryClaim(last) && last.Execute());
        q.Progress(value, completed); CHECK(value == 1 && completed == third);
    }
    {
        Queue q;
        std::promise<void> started;
        auto worker = std::async(std::launch::async, [&] {
            started.set_value();
            Queue::Claim claim;
            if (!q.WaitClaim(claim) || !claim.Execute()) return false;
            Queue::Claim none;
            return !q.WaitClaim(none);
        });
        started.get_future().wait();
        CHECK(q.Enqueue(Queue::Operation::Set, 17) == Queue::EnqueueResult::Accepted);
        CHECK(q.Close());
        CHECK(worker.get() && q.Value() == 17);
        CHECK(q.DrainUntil(Queue::Clock::now() + 1s) == Queue::DrainResult::DrainedRetained);
        CHECK(q.Seal());
    }
    {
        Queue q;
        CHECK(q.DrainUntil(Queue::Clock::now() + 1s) == Queue::DrainResult::Refused);
        CHECK(q.Enqueue(static_cast<Queue::Operation>(255), 1) == Queue::EnqueueResult::Invalid);
        CHECK(q.Enqueue(Queue::Operation::Set, 10) == Queue::EnqueueResult::Accepted);
        Queue::Claim claim;
        CHECK(q.TryClaim(claim));
        CHECK(!q.TryClaim(claim));
        CHECK(q.Outstanding() == 1);
        CHECK(q.Close() && q.Close());
        CHECK(q.Enqueue(Queue::Operation::Add, 1) == Queue::EnqueueResult::Closed);
        std::promise<void> started;
        auto drain = std::async(std::launch::async, [&] {
            started.set_value();
            return q.DrainUntil(Queue::Clock::now() + 5s);
        });
        started.get_future().wait();
        CHECK(drain.wait_for(30ms) == std::future_status::timeout);
        CHECK(claim.Execute());
        CHECK(!claim.Execute());
        CHECK(drain.get() == Queue::DrainResult::DrainedRetained);
        CHECK(q.Value() == 10 && q.Outstanding() == 0);
        CHECK(q.GetState() == Queue::State::DrainedRetained);
        CHECK(q.Seal());
        CHECK(!q.Seal());
        q.Fault();
        CHECK(q.GetState() == Queue::State::QuiescedRetained);
        CHECK(q.DrainUntil(Queue::Clock::now() + 1s) == Queue::DrainResult::Refused);
    }
    {
        Queue q;
        for (std::size_t i = 0; i < Queue::capacity; ++i)
            CHECK(q.Enqueue(Queue::Operation::Add, 1) == Queue::EnqueueResult::Accepted);
        CHECK(q.Enqueue(Queue::Operation::Add, 1) == Queue::EnqueueResult::Full);
        CHECK(q.Close());
        for (std::size_t i = 0; i < Queue::capacity; ++i) {
            Queue::Claim claim;
            CHECK(q.TryClaim(claim));
            CHECK(claim.Execute());
        }
        Queue::Claim empty;
        CHECK(!q.TryClaim(empty));
        CHECK(q.Value() == Queue::capacity);
        CHECK(q.DrainUntil(Queue::Clock::now() + 1s) == Queue::DrainResult::DrainedRetained);
        CHECK(q.Seal());
    }
    {
        Queue q;
        CHECK(!q.Seal());
        CHECK(q.Close());
        CHECK(q.DrainUntil(Queue::Clock::now() + 1s) == Queue::DrainResult::DrainedRetained);
        CHECK(q.GetState() == Queue::State::DrainedRetained);
        CHECK(q.Enqueue(Queue::Operation::Add, 1) == Queue::EnqueueResult::Closed);
        q.Fault();
        CHECK(q.GetState() == Queue::State::FailedRetained);
        CHECK(!q.Seal());
        CHECK(q.DrainUntil(Queue::Clock::now() + 1s) == Queue::DrainResult::FailedRetained);
    }
    {
        Queue q;
        CHECK(q.Enqueue(Queue::Operation::Set, std::numeric_limits<std::uint64_t>::max()) == Queue::EnqueueResult::Accepted);
        Queue::Claim first; CHECK(q.TryClaim(first) && first.Execute());
        CHECK(q.Enqueue(Queue::Operation::Add, 1) == Queue::EnqueueResult::Accepted);
        Queue::Claim second; CHECK(q.TryClaim(second) && second.Execute());
        CHECK(q.Value() == 0);
    }
    {
        Queue q;
        CHECK(q.Enqueue(Queue::Operation::Add, 4) == Queue::EnqueueResult::Accepted);
        Queue::Claim held; CHECK(q.TryClaim(held));
        CHECK(q.Close());
        CHECK(q.DrainUntil(Queue::Clock::now() - 1s) == Queue::DrainResult::FailedRetained);
        CHECK(q.GetState() == Queue::State::FailedRetained && q.Outstanding() == 1);
        CHECK(!held.Execute());
        CHECK(q.Value() == 0 && q.Outstanding() == 0);
        CHECK(q.DrainUntil(Queue::Clock::now() + 1s) == Queue::DrainResult::FailedRetained);
        CHECK(q.Enqueue(Queue::Operation::Add, 1) == Queue::EnqueueResult::Faulted);
    }
    {
        Queue q;
        CHECK(q.Enqueue(Queue::Operation::Add, 4) == Queue::EnqueueResult::Accepted);
        { Queue::Claim abandoned; CHECK(q.TryClaim(abandoned)); }
        CHECK(q.GetState() == Queue::State::FailedRetained && q.Outstanding() == 1);
        CHECK(!q.Close());
        CHECK(q.DrainUntil(Queue::Clock::now() + 1s) == Queue::DrainResult::FailedRetained);
    }
    {
        Queue q;
        CHECK(q.Enqueue(Queue::Operation::Add, 9) == Queue::EnqueueResult::Accepted);
        CHECK(q.Close());
        CHECK(q.DrainUntil(Queue::Clock::now() - 1s) == Queue::DrainResult::FailedRetained);
        Queue::Claim denied;
        CHECK(!q.TryClaim(denied) && q.Outstanding() == 1);
    }
    {
        Queue q;
        CHECK(q.Enqueue(Queue::Operation::Add, 9) == Queue::EnqueueResult::Accepted);
        Queue::Claim held; CHECK(q.TryClaim(held));
        CHECK(q.Close());
        q.Fault();
        CHECK(!held.Execute());
        CHECK(q.DrainUntil(Queue::Clock::now() + 1s) == Queue::DrainResult::FailedRetained);
    }
}
