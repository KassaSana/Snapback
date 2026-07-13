// Snapback context-recovery state machine. Rust: snapback/tracker.rs.
//
// Watches the focus stream: when the user drifts to a distracting app and then
// returns, it fires a SnapbackPayload describing where they left off ("You were
// editing auth.ts in Snapback"). This is the product's namesake feature.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "engine/classifier.hpp"
#include "types.hpp"

namespace snapback {

enum class DistractionState { Focused, Distracted, Recovering };

// The single context-recovery tracker (Rust: ContextTracker). It owns both the context
// timeline snapshots AND the return-from-distraction snapback, driven by window changes +
// on-task gating + a minimum-distraction threshold. (An earlier, simpler SnapbackTracker
// that keyed off the classifier's focus_state was retired in favor of this faithful port.)
class ContextTracker {
public:
    void reset();
    DistractionState state() const { return state_; }
    void set_prediction_feedback(std::optional<std::string> focus_state,
                                 std::optional<std::string> session_goal);
    void set_snapshot_interval_secs(double secs) { snapshot_interval_secs_ = secs; }
    void set_min_distraction_secs(double secs) { min_distraction_secs_ = secs; }

    std::optional<ContextSnapshotDto> observe_window_change(
        const std::string& app_name,
        const std::string& window_title,
        const std::vector<AppRuleRecord>& app_rules,
        double now_secs,
        const std::string& timestamp);

    std::optional<ContextSnapshotDto> maybe_checkpoint_snapshot(
        const std::vector<AppRuleRecord>& app_rules,
        double now_secs,
        const std::string& timestamp);
    std::optional<ContextSnapshotDto> maybe_checkpoint_snapshot(
        const std::vector<AppRuleRecord>& app_rules,
        double now_secs,
        const std::function<std::string()>& timestamp_factory);

    std::optional<SnapbackPayload> take_pending_snapback();
    void dismiss_recovery(double now_secs);

private:
    struct Snapshot {
        std::string app_name;
        std::string window_title;
        std::string file_hint;
        std::string project_hint;
        std::string summary;
        std::string timestamp;

        bool meaningful() const { return !app_name.empty() || !window_title.empty(); }
    };

    static Snapshot make_snapshot(const std::string& app_name,
                                  const std::string& window_title,
                                  const std::string& timestamp);
    static ContextSnapshotDto to_dto(const Snapshot& snapshot);
    bool is_on_task(const std::string& app_name,
                    const std::string& window_title,
                    const std::vector<AppRuleRecord>& app_rules) const;
    std::optional<SnapbackPayload> build_snapback(double distraction_secs) const;

    DistractionState state_ = DistractionState::Focused;
    Snapshot current_;
    std::optional<Snapshot> last_focus_snapshot_;
    double distraction_started_secs_ = -1.0;
    double focus_started_secs_ = 0.0;
    double last_snapshot_secs_ = 0.0;
    double snapshot_interval_secs_ = 30.0;
    double min_distraction_secs_ = 30.0;
    std::optional<SnapbackPayload> pending_snapback_;
    std::optional<std::string> latest_focus_state_;
    std::optional<std::string> latest_session_goal_;
};

}  // namespace snapback
