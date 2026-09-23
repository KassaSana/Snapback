// Roadmap 14.1 — a waiting writer goes before the next reader.
//
// The property is an ordering, so it is asserted as one: a sequence counter records who got
// the lock first. Nothing here measures time, because a timing assertion is exactly the kind
// of test that passes on a quiet machine and fails on a loaded CI runner.
#include "doctest_wrapper.hpp"

#include <atomic>
#include <mutex>
#include <thread>

#include "util/ranked_mutex.hpp"
#include "util/writer_priority.hpp"

using namespace snapback;

TEST_CASE("WriterPriority: with no writer waiting, a reader does not wait") {
    WriterPriority gate;
    CHECK(gate.waiting() == 0);
    gate.yield_to_writers();  // would hang the test if it waited
    CHECK(gate.waiting() == 0);
}

TEST_CASE("WriterPriority: an announcement is counted until released, and released once") {
    WriterPriority gate;
    {
        WriterPriority::Announce announce(gate);
        CHECK(gate.waiting() == 1);
        announce.release();
        CHECK(gate.waiting() == 0);
        announce.release();  // a second release is a no-op, not a decrement below zero
        CHECK(gate.waiting() == 0);
    }  // and destruction after release does not decrement again
    CHECK(gate.waiting() == 0);
    {
        WriterPriority::Announce unreleased(gate);
        CHECK(gate.waiting() == 1);
    }
    CHECK(gate.waiting() == 0);
}

TEST_CASE("WriterPriority: a writer waiting on a held lock takes it before the reader's next "
          "acquisition") {
    // The shape `bench_budgets.cpp` measured: one reader releasing and immediately
    // reacquiring, one writer blocked in between. Without the gate the reader usually wins,
    // which is the whole defect.
    RankedMutex lock{LockRank::Storage};
    WriterPriority gate;
    std::atomic<int> sequence{0};
    std::atomic<int> writer_order{-1};
    std::atomic<int> reader_order{-1};

    std::unique_lock<RankedMutex> reader_hold(lock);  // the command in progress

    std::thread writer([&] {
        WriterPriority::Announce announce(gate);
        std::lock_guard<RankedMutex> held(lock);
        announce.release();
        writer_order = sequence.fetch_add(1);
    });

    // Release only once the writer has announced, so the test is about what the reader does
    // at the boundary and not about thread start-up order.
    while (gate.waiting() == 0) std::this_thread::yield();
    reader_hold.unlock();

    // The reader's next command.
    gate.yield_to_writers();
    {
        std::lock_guard<RankedMutex> held(lock);
        reader_order = sequence.fetch_add(1);
    }
    writer.join();

    CHECK(writer_order == 0);
    CHECK(reader_order == 1);
    CHECK(gate.waiting() == 0);
}
