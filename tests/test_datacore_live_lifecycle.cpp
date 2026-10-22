// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "../src/game/datacore_live_lifecycle.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

namespace {
using Gate = sco::game::datacore_live::LifecycleGate;

struct Event {
    bool Signal() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            signaled = true;
        }
        changed.notify_all();
        return true;
    }

    bool Wait() {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(5), [&] { return signaled; });
    }

    std::mutex mutex;
    std::condition_variable changed;
    bool signaled = false;
};

void TestSharedLeasesExcludeLifecycle() {
    Gate gate;
    Event readersHeld, releaseReaders, lifecycleEntered;
    unsigned entered = 0;

    std::thread reader([&] {
        {
            Gate::ReadLease first;
            assert(gate.EnterRead(first));
            {
                Gate::ReadLease nested;
                assert(gate.EnterRead(nested));
                readersHeld.Signal();
                assert(releaseReaders.Wait());
            }
        }
    });

    assert(readersHeld.Wait());
    Gate::ExclusiveLease attempted;
    assert(gate.TryEnterExclusive(attempted) == Gate::Status::Busy);
    assert(!attempted);

    std::thread lifecycle([&] {
        Gate::ExclusiveLease exclusive;
        gate.EnterLifecycleOrTerminate(exclusive);
        ++entered;
        lifecycleEntered.Signal();
    });

    releaseReaders.Signal();
    assert(lifecycleEntered.Wait());
    reader.join();
    lifecycle.join();
    assert(entered == 1);
}

void TestExclusiveNestingAndReadRefusal() {
    Gate gate;
    {
        Gate::ExclusiveLease outer;
        assert(gate.EnterExclusive(outer) == Gate::Status::Acquired);
        {
            Gate::ExclusiveLease inner;
            assert(gate.EnterExclusive(inner) == Gate::Status::Acquired);

            Gate::ReadLease read;
            assert(!gate.EnterRead(read));
            assert(!read);
        }
        assert(outer);
    }

    Gate::ReadLease read;
    assert(gate.EnterRead(read));
}

void TestQueuedWriterDoesNotDeadlockNestedReader() {
    Gate gate;
    Event writerEntered;
    std::thread writer;
    {
        Gate::ReadLease outerRead;
        assert(gate.EnterRead(outerRead));
        writer = std::thread([&] {
            Gate::ExclusiveLease exclusive;
            gate.EnterLifecycleOrTerminate(exclusive);
            writerEntered.Signal();
        });
        assert(gate.WaitForQueuedWriterForTesting(std::chrono::seconds(5)));
        {
            Gate::ReadLease nestedRead;
            assert(gate.EnterRead(nestedRead));
        }
    }
    assert(writerEntered.Wait());
    writer.join();
}

void TestReadToExclusiveUpgradeIsDenied() {
    Gate gate;
    Gate::ReadLease read;
    assert(gate.EnterRead(read));

    Gate::ExclusiveLease upgrade;
    assert(gate.TryEnterExclusive(upgrade) == Gate::Status::UpgradeDenied);
    assert(!upgrade);
    assert(gate.EnterExclusive(upgrade) == Gate::Status::UpgradeDenied);
    assert(!upgrade);
}

void TestProcessGateIsRetained() {
    assert(&sco::game::datacore_live::ProcessLifecycleGate() ==
           &sco::game::datacore_live::ProcessLifecycleGate());
}
} // namespace

int main() {
    TestSharedLeasesExcludeLifecycle();
    TestExclusiveNestingAndReadRefusal();
    TestQueuedWriterDoesNotDeadlockNestedReader();
    TestReadToExclusiveUpgradeIsDenied();
    TestProcessGateIsRetained();
    std::puts("PASS test_datacore_live_lifecycle");
}
