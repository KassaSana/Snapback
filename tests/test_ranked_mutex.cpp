// ROADMAP 11.6 — the lock order is now a property of the locks, so it can be tested.
//
// Two things are under test here. The mechanism: does RankedMutex actually catch an
// inversion, and is it still a working mutex? And the thing the mechanism exists for: does
// AppState's real mixed-lock code respect the order it documents?
#include "doctest_wrapper.hpp"

#include <atomic>
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

    // Read paths that reach storage.
    (void)state->health();
    (void)state->get_session(session.session_id);
    (void)state->active_session();
    (void)state->latest_prediction();
    (void)state->prediction_history(5);
    (void)state->focus_summary(20);
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
