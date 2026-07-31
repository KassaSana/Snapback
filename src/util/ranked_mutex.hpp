// Lock ordering, enforced by the type system instead of by review.
//
// ROADMAP 11.6. `AppState` has always documented its lock order in a comment — acquire
// `mutex_` before `storage_mutex_`, never the reverse — and nothing checked it. A comment
// holds only as long as every future author reads it, and the class now has three mutexes,
// three mixed-lock methods, and an IPC command surface that keeps growing. ThreadSanitizer
// does not close the gap either: it reports an inversion only if some test happens to drive
// both orders concurrently, so the absence of a TSan report is not evidence of correctness.
//
// A `RankedMutex` carries its position in the program's lock order and refuses to be
// acquired out of turn. The invariant becomes a property of the lock rather than a claim
// about the code around it, and it is checked on the very first single-threaded run through
// a bad path — no race, no scheduler luck required.
//
// `RankedMutex` satisfies BasicLockable and Lockable, so `std::lock_guard lock(mutex_)`
// deduces it and every existing call site keeps working unchanged.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>

namespace snapback {

// The program's complete lock order, outermost first. A thread may hold locks only in
// strictly increasing rank, so reading this enum top to bottom is reading the invariant.
//
// Ranks are spaced by 100 so a lock can be inserted between two existing ones without
// renumbering the others — renumbering is exactly the edit most likely to be made carelessly.
// Equal ranks are a violation too: two locks nobody has ordered can be taken in either order
// by two threads, which is the deadlock this type exists to prevent.
enum class LockRank : int {
    // AppState::mutex_ — in-memory state. Hot UI reads take only this one.
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

// What went wrong. `held` is the innermost rank the thread already had; `requested` is the
// one it tried to take. `out_of_order_release` distinguishes the two failure kinds: false
// means an inverted acquisition (the deadlock risk), true means a lock was released while a
// deeper one was still held, which breaks the LIFO assumption the tracking itself relies on.
struct LockOrderViolation {
    LockRank held;
    LockRank requested;
    bool out_of_order_release = false;
};

using LockOrderViolationHandler = std::function<void(const LockOrderViolation&)>;

namespace detail {

// The ranks this thread currently holds.
//
// A fixed array rather than a std::vector: this runs on every acquisition of the engine
// tick's state lock, so it must not allocate. Eight slots is far more than the three ranks
// that exist; going deeper than that would itself be the bug.
//
// It is a set, not a single "innermost" value, because a lock can be released while a deeper
// one is still held. `std::unique_lock` permits that, and the first draft of this file used a
// single int that each mutex restored on unlock — which is only correct under LIFO release.
// An out-of-LIFO release left the tracking permanently wrong and every subsequent lock on
// that thread reported a violation that had not happened. A check that goes wrong after the
// first mistake is worse than no check: it converts one bug into a wall of false reports.
inline constexpr int kMaxHeldRanks = 8;

struct HeldRanks {
    int ranks[kMaxHeldRanks] = {};
    int count = 0;

    // The deepest rank held. 0 when nothing is held, which is below every real rank.
    int innermost() const {
        int deepest = 0;
        for (int i = 0; i < count; ++i) {
            if (ranks[i] > deepest) deepest = ranks[i];
        }
        return deepest;
    }

    void push(int rank) {
        // Past capacity we stop tracking rather than overwrite. Acquisitions still work; the
        // check degrades to permissive, which is the safe direction for a diagnostic.
        if (count < kMaxHeldRanks) ranks[count++] = rank;
    }

    // Removes the most recent entry for `rank` and reports whether it was the deepest one
    // held — i.e. whether this release was in LIFO order.
    bool pop(int rank) {
        const bool was_innermost = count > 0 && ranks[count - 1] == rank;
        for (int i = count - 1; i >= 0; --i) {
            if (ranks[i] != rank) continue;
            for (int j = i; j + 1 < count; ++j) ranks[j] = ranks[j + 1];
            --count;
            break;
        }
        return was_innermost;
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
    // A violation is a latent deadlock, and a deadlock in the field is a hang with no
    // diagnosis attached. Abort while a developer is watching, so the stack trace names the
    // call site. Release builds only log: crashing a user's app over a warning about a
    // deadlock that has not happened is worse than the warning.
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

// A std::mutex that knows where it sits in the lock order.
//
// Reporting happens *before* the underlying lock is taken, so the handler runs while the
// offending thread still holds only the locks it started with — a handler that aborts gets a
// stack whose frames are the actual acquisition path.
class RankedMutex {
public:
    explicit RankedMutex(LockRank rank) : rank_(static_cast<int>(rank)) {}

    RankedMutex(const RankedMutex&) = delete;
    RankedMutex& operator=(const RankedMutex&) = delete;

    void lock() {
        check_acquire();
        mutex_.lock();
        enter();
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
        if (!held.pop(rank_)) {
            report({static_cast<LockRank>(innermost), static_cast<LockRank>(rank_), true});
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

    void enter() { detail::held_ranks().push(rank_); }

    static void report(const LockOrderViolation& violation) {
        if (const auto& handler = detail::violation_handler()) {
            handler(violation);
        }
    }

    std::mutex mutex_;
    int rank_;
};

}  // namespace snapback
