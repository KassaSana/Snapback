// ROADMAP 11.6 — the lock order is now a property of the locks, so it can be tested.
//
// Two things are under test here. The mechanism: does RankedMutex actually catch an
// inversion, and is it still a working mutex? And the thing the mechanism exists for: does
// AppState's real mixed-lock code respect the order it documents?
#include "doctest_wrapper.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "app/state.hpp"
#include "storage/storage.hpp"
#include "util/ranked_mutex.hpp"

using namespace snapback;

namespace {

// Collects violations instead of aborting, so a provoked inversion is an assertion rather
// than a dead test binary.
class ViolationRecorder {
public:
    ViolationRecorder()
        : scope_([this](const LockOrderViolation& violation) { seen_.push_back(violation); }) {}

    const std::vector<LockOrderViolation>& seen() const { return seen_; }
    bool empty() const { return seen_.empty(); }

private:
    std::vector<LockOrderViolation> seen_;
    ScopedLockOrderHandler scope_;
};

std::unique_ptr<AppState> make_state() {
    auto storage = Storage::open_memory();
    if (!storage) throw std::runtime_error("failed to open in-memory storage");
    return std::make_unique<AppState>(std::move(*storage));
}

}  // namespace

TEST_CASE("ranked mutex accepts locks taken in increasing rank") {
    ViolationRecorder recorder;
    RankedMutex outer{LockRank::State};
    RankedMutex middle{LockRank::ActivityBoundary};
    RankedMutex inner{LockRank::Storage};

    {
        std::lock_guard outer_lock(outer);
        std::lock_guard middle_lock(middle);
        std::lock_guard inner_lock(inner);
    }

    CHECK(recorder.empty());
}

TEST_CASE("ranked mutex accepts skipping a rank") {
    // AppState does exactly this: most mixed-lock methods take State then Storage without
    // touching ActivityBoundary. Skipping inward is safe; only going back outward is not.
    ViolationRecorder recorder;
    RankedMutex outer{LockRank::State};
    RankedMutex inner{LockRank::Storage};

    {
        std::lock_guard outer_lock(outer);
        std::lock_guard inner_lock(inner);
    }

    CHECK(recorder.empty());
}

TEST_CASE("ranked mutex reports an inverted acquisition") {
    ViolationRecorder recorder;
    RankedMutex outer{LockRank::State};
    RankedMutex inner{LockRank::Storage};

    {
        std::lock_guard inner_lock(inner);
        std::lock_guard outer_lock(outer);
    }

    REQUIRE(recorder.seen().size() == 1);
    CHECK(recorder.seen()[0].held == LockRank::Storage);
    CHECK(recorder.seen()[0].requested == LockRank::State);
    CHECK_FALSE(recorder.seen()[0].out_of_order_release);
}

TEST_CASE("ranked mutex reports two locks of equal rank") {
    // Nobody has ordered these two relative to each other, so two threads can take them in
    // opposite orders. That is the deadlock, even though neither acquisition looks inverted.
    ViolationRecorder recorder;
    RankedMutex first{LockRank::Storage};
    RankedMutex second{LockRank::Storage};

    {
        std::lock_guard first_lock(first);
        std::lock_guard second_lock(second);
    }

    REQUIRE(recorder.seen().size() == 1);
    CHECK(recorder.seen()[0].held == LockRank::Storage);
    CHECK(recorder.seen()[0].requested == LockRank::Storage);
}

TEST_CASE("ranked mutex forgets a rank once it is released") {
    // Sequential use is not nested use. Taking Storage, releasing it, then taking State must
    // stay legal or half the IPC surface would report a false violation.
    ViolationRecorder recorder;
    RankedMutex outer{LockRank::State};
    RankedMutex inner{LockRank::Storage};

    { std::lock_guard inner_lock(inner); }
    { std::lock_guard outer_lock(outer); }

    CHECK(recorder.empty());
}

TEST_CASE("ranked mutex restores the enclosing rank after an inner unlock") {
    // The check is only as good as its bookkeeping: after an inner lock is released the
    // thread must still be known to hold the outer one, or the next inversion goes unseen.
    ViolationRecorder recorder;
    RankedMutex outer{LockRank::ActivityBoundary};
    RankedMutex inner{LockRank::Storage};
    RankedMutex violating{LockRank::State};

    {
        std::lock_guard outer_lock(outer);
        { std::lock_guard inner_lock(inner); }
        std::lock_guard bad_lock(violating);
    }

    REQUIRE(recorder.seen().size() == 1);
    CHECK(recorder.seen()[0].held == LockRank::ActivityBoundary);
    CHECK(recorder.seen()[0].requested == LockRank::State);
}

TEST_CASE("ranked mutex reports releasing an outer lock before an inner one") {
    ViolationRecorder recorder;
    RankedMutex outer{LockRank::State};
    RankedMutex inner{LockRank::Storage};

    std::unique_lock outer_lock(outer);
    std::unique_lock inner_lock(inner);
    outer_lock.unlock();  // inner is still held
    inner_lock.unlock();

    REQUIRE(recorder.seen().size() == 1);
    CHECK(recorder.seen()[0].out_of_order_release);
    CHECK(recorder.seen()[0].held == LockRank::Storage);
    CHECK(recorder.seen()[0].requested == LockRank::State);
}

TEST_CASE("ranked mutex tracks rank per thread") {
    // One thread holding Storage must not make another thread's State acquisition look
    // inverted, or the check would fire constantly on the engine/UI split it is guarding.
    ViolationRecorder recorder;
    RankedMutex inner{LockRank::Storage};
    RankedMutex outer{LockRank::State};

    std::lock_guard inner_lock(inner);
    std::thread other([&outer] { std::lock_guard outer_lock(outer); });
    other.join();

    CHECK(recorder.empty());
}

TEST_CASE("ranked mutex still provides mutual exclusion") {
    RankedMutex mutex{LockRank::State};
    int counter = 0;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&mutex, &counter] {
            for (int i = 0; i < 2000; ++i) {
                std::lock_guard lock(mutex);
                ++counter;
            }
        });
    }
    for (auto& thread : threads) thread.join();

    CHECK(counter == 8000);
}

TEST_CASE("ranked mutex supports try_lock") {
    ViolationRecorder recorder;
    RankedMutex mutex{LockRank::State};

    REQUIRE(mutex.try_lock());
    // A second thread must fail to take it, and failing must not corrupt the rank tracking.
    std::atomic<bool> acquired{true};
    std::thread other([&mutex, &acquired] { acquired.store(mutex.try_lock()); });
    other.join();
    CHECK_FALSE(acquired.load());
    mutex.unlock();

    REQUIRE(mutex.try_lock());
    mutex.unlock();
    CHECK(recorder.empty());
}

TEST_CASE("lock metric buckets are powers of two and the top one saturates") {
    // The bucket arithmetic is the whole reason a percentile read off this histogram is an
    // upper bound rather than a value, so it is pinned directly instead of only through the
    // timing cases below, which cannot be exact about anything.
    CHECK(lock_bucket_for_us(0) == 0);
    CHECK(lock_bucket_for_us(1) == 1);
    CHECK(lock_bucket_for_us(2) == 2);
    CHECK(lock_bucket_for_us(3) == 2);
    CHECK(lock_bucket_for_us(4) == 3);
    CHECK(lock_bucket_for_us(1023) == 10);
    CHECK(lock_bucket_for_us(1024) == 11);

    CHECK(lock_bucket_upper_us(0) == 0);
    CHECK(lock_bucket_upper_us(1) == 1);
    CHECK(lock_bucket_upper_us(10) == 1023);

    // Anything past the top bucket lands in it rather than indexing past the array or
    // wrapping to a small bucket, which would report a multi-second hold as a fast one.
    const int top = kLockHistogramBuckets - 1;
    CHECK(lock_bucket_for_us(1ull << 40) == top);
    CHECK(lock_bucket_for_us(~0ull) == top);
}

TEST_CASE("a held lock records its hold time and an uncontended acquisition") {
    reset_lock_metrics();
    RankedMutex mutex{LockRank::Storage};
    {
        std::lock_guard lock(mutex);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const auto metrics = lock_metrics(LockRank::Storage);
    CHECK(metrics.acquisitions == 1);
    CHECK(metrics.contended == 0);
    // Deliberately loose: a loaded machine can stretch a 20 ms sleep but cannot shorten it,
    // so the lower bound is the only side that is safe to assert.
    CHECK(metrics.max_hold_us >= 10000);
    CHECK(metrics.total_hold_us >= 10000);
    CHECK(metrics.hold_p50_us >= 10000);
    // Nothing waited, so the wait histogram is empty rather than zero-valued.
    CHECK(metrics.max_wait_us == 0);
    CHECK(metrics.wait_p95_us == 0);
    reset_lock_metrics();
}

TEST_CASE("a blocked acquisition records a wait and counts as contended") {
    reset_lock_metrics();
    RankedMutex mutex{LockRank::Storage};
    std::atomic<bool> holding{false};

    std::thread holder([&] {
        std::lock_guard lock(mutex);
        holding.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    });
    // Waiting for the flag is what makes this deterministic: without it the second thread can
    // win the lock outright and the test asserts on a contention that never happened.
    while (!holding.load(std::memory_order_acquire)) std::this_thread::yield();
    {
        std::lock_guard lock(mutex);
    }
    holder.join();

    const auto metrics = lock_metrics(LockRank::Storage);
    CHECK(metrics.acquisitions == 2);
    CHECK(metrics.contended == 1);
    CHECK(metrics.max_wait_us > 0);
    CHECK(metrics.wait_p95_us > 0);
    reset_lock_metrics();
}

TEST_CASE("a percentile is a bucket bound and the outlier survives in the maximum") {
    // The property that makes these figures reportable: p95 over twenty samples is the
    // nineteenth, so one slow acquisition must not move it -- and must still be visible, which
    // is what max_hold_us is for. A report that showed only percentiles would hide it.
    reset_lock_metrics();
    RankedMutex mutex{LockRank::Storage};
    for (int i = 0; i < 19; ++i) {
        std::lock_guard lock(mutex);
    }
    {
        std::lock_guard lock(mutex);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const auto metrics = lock_metrics(LockRank::Storage);
    CHECK(metrics.acquisitions == 20);
    CHECK(metrics.max_hold_us >= 10000);
    CHECK(metrics.hold_p50_us < 1000);
    CHECK(metrics.hold_p95_us < metrics.max_hold_us);
    reset_lock_metrics();
}

TEST_CASE("lock metrics are kept per rank") {
    reset_lock_metrics();
    RankedMutex state{LockRank::State};
    RankedMutex storage{LockRank::Storage};
    {
        std::lock_guard state_lock(state);
        std::lock_guard storage_lock(storage);
    }
    {
        std::lock_guard storage_lock(storage);
    }

    CHECK(lock_metrics(LockRank::State).acquisitions == 1);
    CHECK(lock_metrics(LockRank::Storage).acquisitions == 2);
    CHECK(lock_metrics(LockRank::ActivityBoundary).acquisitions == 0);
    reset_lock_metrics();
}

TEST_CASE("app state respects its own lock order") {
    // The regression test that matters, and the reason it names methods one by one: this
    // catches an inversion only in the code paths it actually calls. An earlier draft called
    // eight methods, an inversion was planted in a ninth (upsert_app_rule) to check the guard
    // bites, and the test stayed green. The list below is every AppState method that touches
    // storage_mutex_ or activity_boundary_mutex_ — add to it when a new one appears.
    ViolationRecorder recorder;
    auto state = make_state();

    const auto session = state->start_session("Ranked mutex check", FocusMode::Normal);
    state->set_focus_mode(FocusMode::Deep);
    state->set_private_mode(true);
    state->set_private_mode(false);
    state->set_privacy_exclusions({"Messages"});

    // App rules: the only methods that write storage and refresh an in-memory cache.
    const auto rule = state->upsert_app_rule("Figma", AppRuleKind::Block, "design tool");
    (void)state->app_rules();
    state->delete_app_rule(rule.id);

    // Read paths (the first three are snapshot-backed; the remainder reach storage).
    (void)state->health();
    (void)state->get_session(session.session_id);
    (void)state->active_session();
    (void)state->latest_prediction();
    (void)state->prediction_history(5);
    (void)state->focus_summary_for_window("day");
    (void)state->context_timeline(session.session_id, 5);
    (void)state->session_history(5);
    (void)state->analytics();
    (void)state->summary_report("day");
    (void)state->session_recap(session.session_id);

    state->submit_label(session.session_id, FocusLabel::DeepFocus, "test", "");
    (void)state->stop_session(session.session_id);
    (void)state->delete_session(session.session_id);
    state->delete_all_activity_data();  // takes all three locks

    CHECK(recorder.empty());
}
