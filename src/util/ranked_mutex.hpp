// Lock ordering, enforced at runtime. A RankedMutex knows its position in the program's lock
// order and refuses to be acquired out of turn, so an inversion is caught on the first
// single-threaded run through the bad path (TSan only catches it if a test races both orders).
//
// RankedMutex is BasicLockable and Lockable, so `std::lock_guard lock(mutex_)` works as-is.
#pragma once

#include <array>
#include <cmath>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>

namespace snapback {

// The complete lock order, outermost first; a thread may hold locks only in strictly increasing
// rank. Spaced by 100 so a lock can be inserted without renumbering. Equal ranks are a
// violation.
enum class LockRank : int {
    // AppState::mutex_ — mutable in-memory state. Hot live reads use AppState's immutable
    // snapshot; mutations and less-frequent configuration reads still take this lock.
    State = 100,
    // AppState::activity_boundary_mutex_ — fences the off-lock persistence phase against
    // activity deletion.
    ActivityBoundary = 200,
    // AppState::storage_mutex_ — serializes all Storage access. Innermost, because storage
    // I/O must never block a UI read that only wants in-memory state.
    Storage = 300,
};

inline const char* lock_rank_name(LockRank rank) {
    switch (rank) {
        case LockRank::State:
            return "State";
        case LockRank::ActivityBoundary:
            return "ActivityBoundary";
        case LockRank::Storage:
            return "Storage";
    }
    return "Unknown";
}

// `held` is the innermost rank already held; `requested` the one being taken.
// `out_of_order_release` distinguishes a non-LIFO release from an inverted acquisition.
struct LockOrderViolation {
    LockRank held;
    LockRank requested;
    bool out_of_order_release = false;
};

using LockOrderViolationHandler = std::function<void(const LockOrderViolation&)>;

namespace detail {

// The ranks this thread holds. A fixed array so acquisition never allocates. A set rather than
// one "innermost" value, because std::unique_lock allows non-LIFO release.
inline constexpr int kMaxHeldRanks = 8;

// What a release found: whether it was LIFO, and when the lock was taken (for hold time).
struct ReleasedRank {
    bool was_innermost = false;
    bool timed = false;
    std::int64_t acquired_ns = 0;
};

struct HeldRanks {
    int ranks[kMaxHeldRanks] = {};
    // Parallel to `ranks`, and a fixed array for the same reason: this runs on every
    // acquisition of the engine tick's state lock and must not allocate.
    std::int64_t acquired_ns[kMaxHeldRanks] = {};
    int count = 0;

    // The deepest rank held. 0 when nothing is held, which is below every real rank.
    int innermost() const {
        int deepest = 0;
        for (int i = 0; i < count; ++i) {
            if (ranks[i] > deepest) deepest = ranks[i];
        }
        return deepest;
    }

    void push(int rank, std::int64_t now_ns) {
        // Past capacity we stop tracking rather than overwrite. Acquisitions still work; the
        // check degrades to permissive, which is the safe direction for a diagnostic.
        if (count >= kMaxHeldRanks) return;
        acquired_ns[count] = now_ns;
        ranks[count++] = rank;
    }

    // Removes the most recent entry for `rank`; reports whether it was the deepest held and
    // when it was acquired. Ranks past kMaxHeldRanks were never recorded, so no sample.
    ReleasedRank pop(int rank) {
        ReleasedRank released;
        released.was_innermost = count > 0 && ranks[count - 1] == rank;
        for (int i = count - 1; i >= 0; --i) {
            if (ranks[i] != rank) continue;
            released.timed = true;
            released.acquired_ns = acquired_ns[i];
            for (int j = i; j + 1 < count; ++j) {
                ranks[j] = ranks[j + 1];
                acquired_ns[j] = acquired_ns[j + 1];
            }
            --count;
            break;
        }
        return released;
    }
};

inline HeldRanks& held_ranks() {
    static thread_local HeldRanks held;
    return held;
}

inline void default_violation_handler(const LockOrderViolation& violation) {
    if (violation.out_of_order_release) {
        std::fprintf(stderr,
                     "[snapback] lock-order violation: released %s while holding %s. "
                     "Locks must be released innermost-first.\n",
                     lock_rank_name(violation.requested), lock_rank_name(violation.held));
    } else {
        std::fprintf(stderr,
                     "[snapback] lock-order violation: tried to acquire %s while holding %s. "
                     "Locks must be acquired in increasing rank (see LockRank).\n",
                     lock_rank_name(violation.requested), lock_rank_name(violation.held));
    }
#ifndef NDEBUG
    // Debug aborts so the stack names the call site; Release only logs.
    std::abort();
#endif
}

inline LockOrderViolationHandler& violation_handler() {
    static LockOrderViolationHandler handler = default_violation_handler;
    return handler;
}

}  // namespace detail

// Installs a violation handler for the duration of a scope and restores the previous one.
// This is what makes the invariant testable: a test can provoke an inversion, observe that
// it was reported, and keep running instead of aborting the whole suite.
class ScopedLockOrderHandler {
public:
    explicit ScopedLockOrderHandler(LockOrderViolationHandler handler)
        : previous_(detail::violation_handler()) {
        detail::violation_handler() = std::move(handler);
    }
    ~ScopedLockOrderHandler() { detail::violation_handler() = std::move(previous_); }

    ScopedLockOrderHandler(const ScopedLockOrderHandler&) = delete;
    ScopedLockOrderHandler& operator=(const ScopedLockOrderHandler&) = delete;

private:
    LockOrderViolationHandler previous_;
};

// ---- Lock metrics ------------------------------------------------------------------------
//
// Hold and wait times, measured in RankedMutex itself so every acquisition is counted. Ships in
// Release so real installs report them (via get_diagnostics and support bundles). Cost: two
// steady_clock reads per acquisition; waits are timed only on the slow path.

// Log-scale buckets: bucket 0 is under 1 us, bucket i covers [2^(i-1), 2^i) us, the top one
// saturates. A percentile read off buckets is an upper bound; the exact tail is max_*_us.
inline constexpr int kLockHistogramBuckets = 24;

inline int lock_bucket_for_us(std::uint64_t microseconds) {
    int bucket = 0;
    while (microseconds > 0 && bucket + 1 < kLockHistogramBuckets) {
        microseconds >>= 1;
        ++bucket;
    }
    return bucket;
}

// The largest duration bucket `index` can hold. The top bucket is open-ended, so its "bound"
// is really a floor; a caller that cares about the true tail reads `max_hold_us`.
inline std::uint64_t lock_bucket_upper_us(int index) {
    if (index <= 0) return 0;
    return (static_cast<std::uint64_t>(1) << index) - 1;
}

struct LockMetricsSnapshot {
    std::uint64_t acquisitions{};
    // Acquisitions where someone else already held the lock. Counted exactly, by the
    // try_lock fast path failing, rather than inferred from a wait that rounded above zero.
    std::uint64_t contended{};
    std::uint64_t total_hold_us{};
    std::uint64_t max_hold_us{};
    std::uint64_t max_wait_us{};
    // Upper bounds, per the histogram note above.
    std::uint64_t hold_p50_us{};
    std::uint64_t hold_p95_us{};
    std::uint64_t wait_p50_us{};
    std::uint64_t wait_p95_us{};
};

namespace detail {

struct LockMetrics {
    std::atomic<std::uint64_t> acquisitions{0};
    std::atomic<std::uint64_t> contended{0};
    std::atomic<std::uint64_t> total_hold_us{0};
    std::atomic<std::uint64_t> max_hold_us{0};
    std::atomic<std::uint64_t> max_wait_us{0};
    std::array<std::atomic<std::uint64_t>, kLockHistogramBuckets> hold{};
    std::array<std::atomic<std::uint64_t>, kLockHistogramBuckets> wait{};
};

// Indexed by rank/100, which is how LockRank is spaced. Sized past the three that exist so
// inserting a rank between two others -- the reason for that spacing -- needs no edit here.
inline constexpr int kLockMetricSlots = 8;

inline int lock_metric_slot(int rank) {
    const int slot = rank / 100;
    if (slot < 0) return 0;
    return slot < kLockMetricSlots ? slot : kLockMetricSlots - 1;
}

inline LockMetrics& lock_metrics_for(int rank) {
    static LockMetrics slots[kLockMetricSlots];
    return slots[lock_metric_slot(rank)];
}

inline std::int64_t steady_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline void store_max(std::atomic<std::uint64_t>& target, std::uint64_t value) {
    auto seen = target.load(std::memory_order_relaxed);
    while (value > seen &&
           !target.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
    }
}

inline void record_sample(std::array<std::atomic<std::uint64_t>, kLockHistogramBuckets>& buckets,
                          std::atomic<std::uint64_t>& max_us, std::int64_t elapsed_ns) {
    // A steady clock cannot run backwards, but a negative value would index outside the
    // array, so it is floored rather than trusted.
    const std::uint64_t us =
        elapsed_ns > 0 ? static_cast<std::uint64_t>(elapsed_ns) / 1000u : 0u;
    buckets[static_cast<std::size_t>(lock_bucket_for_us(us))].fetch_add(
        1, std::memory_order_relaxed);
    store_max(max_us, us);
}

inline std::uint64_t percentile_upper_us(
    const std::array<std::atomic<std::uint64_t>, kLockHistogramBuckets>& buckets,
    double fraction) {
    std::uint64_t total = 0;
    for (const auto& bucket : buckets) total += bucket.load(std::memory_order_relaxed);
    if (total == 0) return 0;
    // Round the target up: with 20 samples, p95 must be the 19th, not the 19.0th.
    const auto target =
        static_cast<std::uint64_t>(std::ceil(static_cast<double>(total) * fraction));
    std::uint64_t seen = 0;
    for (int i = 0; i < kLockHistogramBuckets; ++i) {
        seen += buckets[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
        if (seen >= target) return lock_bucket_upper_us(i);
    }
    return lock_bucket_upper_us(kLockHistogramBuckets - 1);
}

}  // namespace detail

// A consistent-enough read for reporting: the counters are relaxed atomics sampled one at a
// time, so a snapshot taken while the engine runs can straddle an acquisition. That is the
// right trade for a diagnostic -- the alternative is a lock around the lock.
inline LockMetricsSnapshot lock_metrics(LockRank rank) {
    const auto& metrics = detail::lock_metrics_for(static_cast<int>(rank));
    LockMetricsSnapshot out;
    out.acquisitions = metrics.acquisitions.load(std::memory_order_relaxed);
    out.contended = metrics.contended.load(std::memory_order_relaxed);
    out.total_hold_us = metrics.total_hold_us.load(std::memory_order_relaxed);
    out.max_hold_us = metrics.max_hold_us.load(std::memory_order_relaxed);
    out.max_wait_us = metrics.max_wait_us.load(std::memory_order_relaxed);
    out.hold_p50_us = detail::percentile_upper_us(metrics.hold, 0.50);
    out.hold_p95_us = detail::percentile_upper_us(metrics.hold, 0.95);
    out.wait_p50_us = detail::percentile_upper_us(metrics.wait, 0.50);
    out.wait_p95_us = detail::percentile_upper_us(metrics.wait, 0.95);
    return out;
}

// Test seam. The counters are process-wide, so without this one case reads another's samples
// and the suite's result depends on execution order.
inline void reset_lock_metrics() {
    for (int slot = 0; slot < detail::kLockMetricSlots; ++slot) {
        auto& metrics = detail::lock_metrics_for(slot * 100);
        metrics.acquisitions.store(0, std::memory_order_relaxed);
        metrics.contended.store(0, std::memory_order_relaxed);
        metrics.total_hold_us.store(0, std::memory_order_relaxed);
        metrics.max_hold_us.store(0, std::memory_order_relaxed);
        metrics.max_wait_us.store(0, std::memory_order_relaxed);
        for (int i = 0; i < kLockHistogramBuckets; ++i) {
            metrics.hold[static_cast<std::size_t>(i)].store(0, std::memory_order_relaxed);
            metrics.wait[static_cast<std::size_t>(i)].store(0, std::memory_order_relaxed);
        }
    }
}

// A std::mutex that knows its place in the lock order. Violations are reported before the lock
// is taken, so an aborting handler's stack shows the real acquisition path.
class RankedMutex {
public:
    explicit RankedMutex(LockRank rank) : rank_(static_cast<int>(rank)) {}

    RankedMutex(const RankedMutex&) = delete;
    RankedMutex& operator=(const RankedMutex&) = delete;

    void lock() {
        check_acquire();
        // The try_lock fast path is what keeps an uncontended acquisition down to one clock
        // read: there is no wait to time, and "contended" is then counted exactly rather
        // than inferred from a wait that happened to round above zero.
        if (mutex_.try_lock()) {
            enter();
            return;
        }
        auto& metrics = detail::lock_metrics_for(rank_);
        metrics.contended.fetch_add(1, std::memory_order_relaxed);
        const auto started_ns = detail::steady_now_ns();
        mutex_.lock();
        const auto acquired_ns = detail::steady_now_ns();
        detail::record_sample(metrics.wait, metrics.max_wait_us, acquired_ns - started_ns);
        enter(acquired_ns);
    }

    bool try_lock() {
        // A failed try_lock acquires nothing, so it cannot invert the order. Check first
        // anyway: code that would deadlock under lock() is a bug worth reporting even on the
        // run where the try happens to fail.
        check_acquire();
        if (!mutex_.try_lock()) {
            return false;
        }
        enter();
        return true;
    }

    void unlock() {
        auto& held = detail::held_ranks();
        const int innermost = held.innermost();
        // Drop our rank first, so the report describes the state that was actually wrong and
        // the bookkeeping is correct either way. A non-LIFO release is reported once and
        // costs nothing afterwards.
        const auto released = held.pop(rank_);
        if (!released.was_innermost) {
            report({static_cast<LockRank>(innermost), static_cast<LockRank>(rank_), true});
        }
        // Sampled before the release, so the span measured is the one the lock was actually
        // held for rather than that span plus whatever the next waiter does first.
        if (released.timed) {
            auto& metrics = detail::lock_metrics_for(rank_);
            const auto held_ns = detail::steady_now_ns() - released.acquired_ns;
            detail::record_sample(metrics.hold, metrics.max_hold_us, held_ns);
            metrics.total_hold_us.fetch_add(
                held_ns > 0 ? static_cast<std::uint64_t>(held_ns) / 1000u : 0u,
                std::memory_order_relaxed);
        }
        mutex_.unlock();
    }

    LockRank rank() const { return static_cast<LockRank>(rank_); }

private:
    void check_acquire() const {
        const int innermost = detail::held_ranks().innermost();
        if (innermost >= rank_) {
            report({static_cast<LockRank>(innermost), static_cast<LockRank>(rank_), false});
        }
    }

    // `acquired_ns` comes in from the contended path, which has already read the clock to
    // close out the wait; the fast path reads it here.
    void enter(std::int64_t acquired_ns = 0) {
        if (acquired_ns == 0) acquired_ns = detail::steady_now_ns();
        detail::lock_metrics_for(rank_).acquisitions.fetch_add(1, std::memory_order_relaxed);
        detail::held_ranks().push(rank_, acquired_ns);
    }

    static void report(const LockOrderViolation& violation) {
        if (const auto& handler = detail::violation_handler()) {
            handler(violation);
        }
    }

    std::mutex mutex_;
    int rank_;
};

}  // namespace snapback
