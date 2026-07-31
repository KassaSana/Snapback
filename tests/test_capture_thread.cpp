// First tests for the capture layer. Before this file, nothing under tests/ referenced
// InputHook, CaptureThread, or query_active_window — the subsystem CLAUDE.md calls out as
// "where bugs will hide" had zero coverage, which is exactly how three macOS capture bugs
// shipped unnoticed.
//
// The OS hooks themselves can't run headlessly, but the part that owns memory safety can:
// CaptureThread's producer/consumer seam over the SPSC ring. A ScriptedHook stands in for
// the platform backend so the producer side is deterministic. (test_ring_buffer.cpp covers
// the ring's memory ordering directly; this covers the class that drives it.)
#include "doctest_wrapper.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

#include "capture/capture_thread.hpp"
#include "capture/input_context.hpp"

using namespace snapback;

namespace {

// Emits `count` events from the hook thread — the ring buffer's only legal producer —
// then idles until stop(), the way a real InputHook blocks on its OS event loop.
class ScriptedHook final : public InputHook {
public:
    explicit ScriptedHook(int count, EventType event_type = EventType::KeyPress)
        : count_(count),
          event_type_(event_type),
          context_(std::make_shared<CaptureContext>(
              CaptureContext{"TestEditor", "capture_thread.cpp"})) {}

    void run(InputCallback on_event, const std::atomic<bool>&) override {
        for (int i = 0; i < count_ && running_.load(std::memory_order_relaxed); ++i) {
            CaptureEvent ev;
            ev.event_type = event_type_;
            ev.timestamp_secs = static_cast<double>(i);
            ev.captured_context = context_;
            on_event(std::move(ev));
        }
        // Release: pairs with the acquire in emitted(), so a consumer that observes this
        // flag is guaranteed to see every push above.
        emitted_.store(true, std::memory_order_release);
        while (running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void stop() noexcept override { running_.store(false, std::memory_order_relaxed); }

    bool emitted() const { return emitted_.load(std::memory_order_acquire); }
    bool stopped() const { return !running_.load(std::memory_order_acquire); }

private:
    int count_;
    EventType event_type_;
    std::shared_ptr<const CaptureContext> context_;
    std::atomic<bool> running_{true};
    std::atomic<bool> emitted_{false};
};

class ReturningHook final : public InputHook {
public:
    void run(InputCallback, const std::atomic<bool>&) override {
        returned_.store(true, std::memory_order_release);
    }
    void stop() noexcept override {}

    bool returned() const { return returned_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> returned_{false};
};

class DelayedEntryHook final : public InputHook {
public:
    void run(InputCallback, const std::atomic<bool>& stop_requested) override {
        while (!released_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        saw_stop_on_entry_.store(stop_requested.load(std::memory_order_acquire),
                                 std::memory_order_release);
        while (!stop_requested.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    void stop() noexcept override { released_.store(true, std::memory_order_release); }

    bool saw_stop_on_entry() const {
        return saw_stop_on_entry_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> released_{false};
    std::atomic<bool> saw_stop_on_entry_{false};
};

// Bounded wait: a bug fails the test instead of hanging CI.
bool wait_for_emit(const ScriptedHook& hook) {
    for (int i = 0; i < 5000; ++i) {
        if (hook.emitted()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool wait_for_return(const ReturningHook& hook) {
    for (int i = 0; i < 5000; ++i) {
        if (hook.returned()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool wait_for_capture_stop(const CaptureThread& capture) {
    for (int i = 0; i < 5000; ++i) {
        if (!capture.running()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

std::size_t drain(CaptureThread& capture) {
    std::size_t count = 0;
    while (capture.next_event()) ++count;
    return count;
}

}  // namespace

// Regression guard for Roadmap 6.1: RingBuffer used to hold its 65,536-slot array inline,
// making sizeof(CaptureThread) ~6 MB — every stack-allocated instance (this file's tests,
// any future AppState local) overflowed Windows' 1 MB default thread stack. The storage now
// lives on the heap; if someone inlines it again, this fails at compile time instead of
// SIGSEGV-ing only on Windows CI.
static_assert(sizeof(CaptureThread) < 4096,
              "CaptureThread must stay stack-friendly; ring storage belongs on the heap");

TEST_CASE("input context fails closed when the foreground window changes") {
    int captured_window = 0;
    int other_window = 0;

    CHECK(detail::context_matches_foreground(&captured_window, &captured_window, true));
    CHECK_FALSE(detail::context_matches_foreground(&other_window, &captured_window, true));
    CHECK_FALSE(detail::context_matches_foreground(nullptr, &captured_window, true));
    CHECK_FALSE(detail::context_matches_foreground(&captured_window, &captured_window, false));
}

TEST_CASE("CaptureThread drains hook events in FIFO order") {
    ScriptedHook hook(10);
    CaptureThread capture;
    capture.start(&hook);
    REQUIRE(wait_for_emit(hook));

    int drained = 0;
    while (auto ev = capture.next_event()) {
        CHECK(ev->event_type == EventType::KeyPress);
        // The ring must preserve order: event i carries timestamp i.
        CHECK(ev->timestamp_secs == doctest::Approx(static_cast<double>(drained)));
        CHECK(ev->app_name == "TestEditor");
        CHECK(ev->window_title == "capture_thread.cpp");
        CHECK_FALSE(ev->captured_context);
        ++drained;
    }
    CHECK(drained == 10);
    CHECK(capture.last_event_age_ms().has_value());
    CHECK(capture.input_observed());
    CHECK(capture.events_dropped() == 0);
    capture.stop();
}

TEST_CASE("CaptureThread does not treat polling-only context as observed input") {
    ScriptedHook hook(1, EventType::WindowFocusChange);
    CaptureThread capture;
    capture.start(&hook);
    REQUIRE(wait_for_emit(hook));

    CHECK(capture.last_event_age_ms().has_value());
    CHECK_FALSE(capture.input_observed());
    capture.stop();
}

TEST_CASE("CaptureThread reports a hook that returns as failed") {
    ReturningHook hook;
    CaptureThread capture;
    capture.start(&hook);
    REQUIRE(wait_for_return(hook));
    // The hook's return flag is set before CaptureThread records the failure and publishes
    // its stopped state. Wait for the class's public completion state, not the fake's
    // internal implementation detail, before asserting the health contract.
    REQUIRE(wait_for_capture_stop(capture));

    CHECK_FALSE(capture.running());
    CHECK(capture.failed());
    REQUIRE(capture.failure_reason().has_value());
    CHECK(*capture.failure_reason() == "input hook stopped unexpectedly");

    ScriptedHook replacement(1);
    capture.start(&replacement);
    REQUIRE(wait_for_emit(replacement));
    CHECK(capture.running());
    capture.stop();
}

TEST_CASE("CaptureThread never reports failed and running at the same time") {
    // ROADMAP 11.1 found this, and the finding is really about how it was found.
    //
    // AppState::health() loads failed() and running() in two separate atomic reads and
    // publishes them as `status` and `captureRunning`. record_failure() used to set failed_
    // first and leave running_ true until the thread body ended, so between those two stores
    // — a mutex acquisition and a string assignment apart — health() could report "capture
    // failed" and "running: true" in the same report. Two fields that contradict each other,
    // same shape as 7.7.
    //
    // The test above sidesteps the window by waiting for running() to go false before it
    // asserts anything. The AppState-level test did *not*, and it failed about 1 run in 40 —
    // a flake that had been dismissed as noise for as long as the whole suite was one CTest
    // entry, because one bad case in a 295-case process reads as "the suite is flaky."
    // Registering cases individually is what turned it into a reproducible single failure.
    //
    // This samples the window deliberately: spin, don't sleep, so the observation lands
    // inside the microseconds between the two stores. With the stores in the wrong order
    // this fails; with them in the right order it is 0 for 200.
    constexpr int kAttempts = 200;
    int contradictions = 0;

    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        ReturningHook hook;
        CaptureThread capture;
        capture.start(&hook);

        bool failed = false;
        for (int spin = 0; spin < 2'000'000 && !failed; ++spin) {
            failed = capture.failed();
        }
        REQUIRE(failed);

        // The invariant: if the class says it failed, it must already say it is not running.
        if (capture.running()) ++contradictions;
        capture.stop();
    }

    CHECK(contradictions == 0);
}

TEST_CASE("CaptureThread counts drops once the ring is full") {
    // The ring holds kCapacity - 1 events: one slot stays empty so head == tail can mean
    // "empty" unambiguously. So pushing kCapacity + N yields N + 1 drops.
    constexpr int kOverflow = 128;
    ScriptedHook hook(static_cast<int>(CaptureThread::kCapacity) + kOverflow);
    CaptureThread capture;
    capture.start(&hook);
    REQUIRE(wait_for_emit(hook));

    CHECK(drain(capture) == CaptureThread::kCapacity - 1);
    CHECK(capture.events_dropped() == kOverflow + 1);
    capture.stop();
}

TEST_CASE("CaptureThread resets its drop count for each capture run") {
    constexpr int kOverflow = 8;
    ScriptedHook first(static_cast<int>(CaptureThread::kCapacity) + kOverflow);
    CaptureThread capture;
    capture.start(&first);
    REQUIRE(wait_for_emit(first));
    CHECK(capture.events_dropped() == kOverflow + 1);
    capture.stop();
    CHECK(drain(capture) == CaptureThread::kCapacity - 1);

    ScriptedHook second(1);
    capture.start(&second);
    REQUIRE(wait_for_emit(second));
    CHECK(capture.events_dropped() == 0);
    capture.stop();
}

TEST_CASE("CaptureThread ignores a second start") {
    // Regression guard: this used to assign over a joinable std::thread, which calls
    // std::terminate. It was survivable only because AppState::start_engine happened to
    // gate it with a CAS — the invariant lived in the caller, not the class.
    ScriptedHook hook(1);
    CaptureThread capture;
    capture.start(&hook);
    capture.start(&hook);
    CHECK(capture.running());

    capture.stop();
    CHECK_FALSE(capture.running());
}

TEST_CASE("CaptureThread stop is safe without a start") {
    CaptureThread capture;
    capture.stop();
    CHECK_FALSE(capture.running());
}

TEST_CASE("CaptureThread destruction stops a running hook") {
    ScriptedHook hook(1);
    {
        CaptureThread capture;
        capture.start(&hook);
        REQUIRE(wait_for_emit(hook));
    }

    CHECK(hook.stopped());
}

TEST_CASE("CaptureThread preserves a stop requested before the hook enters run") {
    DelayedEntryHook hook;
    CaptureThread capture;

    capture.start(&hook);
    capture.stop();

    CHECK(hook.saw_stop_on_entry());
    CHECK_FALSE(capture.running());
    CHECK_FALSE(capture.failed());
}

TEST_CASE("CaptureThread can restart after stop") {
    CaptureThread capture;

    ScriptedHook first(3);
    capture.start(&first);
    REQUIRE(wait_for_emit(first));
    capture.stop();

    ScriptedHook second(2);
    capture.start(&second);
    REQUIRE(wait_for_emit(second));
    capture.stop();

    // Buffered events survive the restart; stop() ends the hook, it doesn't drain.
    CHECK(drain(capture) == 5);
}
