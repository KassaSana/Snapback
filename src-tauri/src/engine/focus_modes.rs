use serde::Serialize;

use crate::types::FocusMode;

#[derive(Debug, Clone, Serialize)]
pub struct HyperfocusAlert {
    pub message: String,
    pub focus_duration_secs: u64,
}

pub fn check_hyperfocus(
    focus_mode: FocusMode,
    deep_focus_secs: u64,
    last_alert_secs: u64,
) -> Option<HyperfocusAlert> {
    let threshold_secs = focus_mode.hyperfocus_minutes() as u64 * 60;
    if deep_focus_secs >= threshold_secs && deep_focus_secs.saturating_sub(last_alert_secs) >= 600 {
        Some(HyperfocusAlert {
            message: format!(
                "You've been in deep focus for {} minutes. Consider a short break.",
                deep_focus_secs / 60
            ),
            focus_duration_secs: deep_focus_secs,
        })
    } else {
        None
    }
}

/// Decide whether to emit a hyperfocus alert for the current engine tick, and
/// return the alert-clock value the caller should carry into the next tick.
///
/// `deep_secs` is how long the *current* deep-focus streak has run (0 when not
/// in a streak). `last_alert_secs` is the streak-relative time of the previous
/// alert, or 0 if none.
///
/// Crucially, when `is_deep` is false the streak has ended, so the returned
/// alert clock resets to 0. Keeping the reset here — rather than at each call
/// site — means the streak timer and the alert clock can't drift out of sync:
/// a stale `last_alert_secs` left over from a prior streak would otherwise
/// suppress the next streak's alert until `deep_secs` climbed past it.
pub fn evaluate_hyperfocus(
    is_deep: bool,
    deep_secs: u64,
    last_alert_secs: u64,
    focus_mode: FocusMode,
) -> (Option<HyperfocusAlert>, u64) {
    if !is_deep {
        return (None, 0);
    }
    match check_hyperfocus(focus_mode, deep_secs, last_alert_secs) {
        Some(alert) => (Some(alert), deep_secs),
        None => (None, last_alert_secs),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::FocusMode;

    #[test]
    fn no_alert_before_hyperfocus_threshold() {
        assert!(check_hyperfocus(FocusMode::Normal, 119 * 60, 0).is_none());
    }

    #[test]
    fn alerts_after_hyperfocus_threshold() {
        let alert = check_hyperfocus(FocusMode::Normal, 130 * 60, 0).expect("alert");
        assert!(alert.message.contains("130 minutes"));
        assert_eq!(alert.focus_duration_secs, 130 * 60);
    }

    #[test]
    fn suppresses_repeat_alerts_within_ten_minutes() {
        assert!(check_hyperfocus(FocusMode::Normal, 130 * 60, 125 * 60).is_none());
    }

    #[test]
    fn alerts_exactly_at_threshold() {
        // Boundary: the gate is `>= threshold`, so exactly at the threshold
        // (120 min in Normal) must fire, not stay silent one tick early.
        let alert = check_hyperfocus(FocusMode::Normal, 120 * 60, 0).expect("alert at threshold");
        assert_eq!(alert.focus_duration_secs, 120 * 60);
    }

    #[test]
    fn re_alert_gap_boundary_is_inclusive_at_600_seconds() {
        // The re-alert gate is `deep_focus_secs - last_alert_secs >= 600`.
        // Exactly 600s of additional deep focus since the last alert must
        // re-fire; one second under (599s) must still be suppressed.
        let at_gap = check_hyperfocus(FocusMode::Normal, 130 * 60, 130 * 60 - 600);
        assert!(at_gap.is_some(), "exactly 600s gap should re-alert");

        let just_under = check_hyperfocus(FocusMode::Normal, 130 * 60, 130 * 60 - 599);
        assert!(just_under.is_none(), "599s gap should stay suppressed");
    }

    #[test]
    fn deep_mode_uses_ninety_minute_threshold() {
        // Deep mode's threshold is 90 min — higher than Normal would imply
        // for the same elapsed time, so a duration that alerts in shorter
        // modes must still be silent just below 90 min here.
        assert!(check_hyperfocus(FocusMode::Deep, 89 * 60, 0).is_none());
        let alert = check_hyperfocus(FocusMode::Deep, 90 * 60, 0).expect("alert at 90 min");
        assert_eq!(alert.focus_duration_secs, 90 * 60);
    }

    #[test]
    fn recovery_mode_uses_forty_five_minute_threshold() {
        // Recovery mode nudges earliest (45 min) — the shortest threshold,
        // so it alerts at a duration that Normal/Deep would still ignore.
        assert!(check_hyperfocus(FocusMode::Recovery, 44 * 60, 0).is_none());
        assert!(check_hyperfocus(FocusMode::Recovery, 45 * 60, 0).is_some());
        // Sanity: the same 45-min duration is below Normal's 120-min gate.
        assert!(check_hyperfocus(FocusMode::Normal, 45 * 60, 0).is_none());
    }

    #[test]
    fn evaluate_hyperfocus_resets_alert_clock_when_leaving_deep_focus() {
        // Leaving deep focus must reset the alert clock to 0 regardless of
        // how large it was — otherwise the next streak inherits a stale value.
        let (alert, next_clock) = evaluate_hyperfocus(false, 0, 9_999, FocusMode::Normal);
        assert!(alert.is_none());
        assert_eq!(next_clock, 0);
    }

    #[test]
    fn evaluate_hyperfocus_alerts_and_advances_clock_within_a_streak() {
        // First alert at the threshold advances the clock to the current
        // streak duration; a moment later (still under the 600s re-alert gap)
        // it stays quiet and holds the clock.
        let (first, clock) = evaluate_hyperfocus(true, 120 * 60, 0, FocusMode::Normal);
        assert!(first.is_some());
        assert_eq!(clock, 120 * 60);

        let (second, clock) = evaluate_hyperfocus(true, 120 * 60 + 300, clock, FocusMode::Normal);
        assert!(second.is_none());
        assert_eq!(clock, 120 * 60, "clock unchanged while suppressed");
    }

    #[test]
    fn evaluate_hyperfocus_second_streak_alerts_on_time_after_reset() {
        // Regression for the engine-loop bug: a first streak alerts at 120 min
        // (clock -> 7200). The user then leaves deep focus, which must reset
        // the clock to 0. A second streak that reaches 120 min must alert
        // again — not be suppressed because the clock still held 7200.
        let (_first, clock) = evaluate_hyperfocus(true, 120 * 60, 0, FocusMode::Normal);
        assert_eq!(clock, 120 * 60);

        // Left deep focus: clock resets.
        let (_none, clock) = evaluate_hyperfocus(false, 0, clock, FocusMode::Normal);
        assert_eq!(clock, 0);

        // New streak reaches the threshold again — must alert.
        let (second, clock) = evaluate_hyperfocus(true, 120 * 60, clock, FocusMode::Normal);
        assert!(
            second.is_some(),
            "second streak should alert after the clock reset"
        );
        assert_eq!(clock, 120 * 60);
    }
}
