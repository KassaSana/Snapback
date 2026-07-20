// First tests for the capture layer. Before this file, nothing under tests/ referenced
// InputHook, CaptureThread, or query_active_window — the subsystem CLAUDE.md calls out as
// "where bugs will hide" had zero coverage, which is exactly how three macOS capture bugs
// shipped unnoticed.
//
// The OS hooks themselves can't run headlessly, but the part that owns memory safety can:
// CaptureThread's producer/consumer seam over the SPSC ring. A ScriptedHook stands in for
// the platform backend so the producer side is deterministic. (test_ring_buffer.cpp covers
// the ring's memory ordering directly; this covers the class that drives it.)
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

#include "capture/capture_thread.hpp"

using namespace snapback;

namespace {

// Emits `count` events from the hook thread — the ring buffer's only legal producer —
// then idles until stop(), the way a real InputHook blocks on its OS event loop.
class ScriptedHook final : public InputHook {
public:
    explicit ScriptedHook(int count) : count_(count) {}

    void run(InputCallback on_event) override {
        for (int i = 0; i < count_ && running_.load(std::memory_order_relaxed); ++i) {
            CaptureEvent ev;
            ev.event_type = EventType::KeyPress;
            ev.timestamp_secs = static_cast<double>(i);
            on_event(ev);
        }
        // Release: pairs with the acquire in emitted(), so a consumer that observes this
        // flag is guaranteed to see every push above.
        emitted_.store(true, std::memory_order_release);
        while (running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void stop() override { running_.store(false, std::memory_order_relaxed); }

    bool emitted() const { return emitted_.load(std::memory_order_acquire); }

private:
    int count_;
    std::atomic<bool> running_{true};
    std::atomic<bool> emitted_{false};
};

// Bounded wait: a bug fails the test instead of hanging CI.
bool wait_for_emit(const ScriptedHook& hook) {
    for (int i = 0; i < 5000; ++i) {
        if (hook.emitted()) return true;
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
        ++drained;
    }
    CHECK(drained == 10);
    CHECK(capture.events_dropped() == 0);
    capture.stop();
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
