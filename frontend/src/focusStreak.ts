// Roadmap 10.13. How the app is allowed to talk about "streaks".
//
// Three nearly identical labels sat over three incompatible quantities. Recent Focus's "Focus
// streak" and Summary's "Best streak" were counts of consecutive non-DISTRACTED **prediction
// rows**; Analytics's "Focus streak" was consecutive completed **sessions** scoring at least
// 70. Predictions arrive when input produces a reading, not once per second, so neither row
// count was elapsed focus by any reading — and two people doing identical work got different
// numbers purely from typing cadence.
//
// The rule this module exists to enforce: **never display a row count with time-like copy.**
// The two row-count tiles are now durations, and the session metric says "sessions" in its
// own label.

/** The unit each metric is actually in, so a label can never be attached to the wrong one. */
export const FOCUS_STRETCH_LABEL = "Longest focus";
export const PRODUCTIVE_SESSIONS_LABEL = "Sessions in a row";

/**
 * A duration in seconds, as a compact tile value.
 *
 * Seconds below a minute so a short stretch is not rounded to "0m"; minutes up to an hour;
 * hours and minutes above that. No "0h 0m" — the shortest true answer is always shown.
 */
export function formatFocusStretch(seconds: number): string {
  const total = Math.max(0, Math.floor(Number(seconds) || 0));
  if (total < 60) return `${total}s`;
  if (total < 3600) return `${Math.floor(total / 60)}m`;
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  return minutes === 0 ? `${hours}h` : `${hours}h ${minutes}m`;
}

/**
 * The accessible/helper sentence under the tile.
 *
 * It names the unit and the break rule, because a duration with no stated boundary invites the
 * reader to assume it means "time in the app", which it does not.
 */
export function focusStretchHelperText(seconds: number): string {
  if (seconds <= 0) {
    return "No unbroken focused stretch recorded yet. A stretch ends at a distraction, a break in recording, or the end of a session.";
  }
  return `Longest unbroken focused stretch: ${formatFocusStretch(seconds)}. A stretch ends at a distraction, a break in recording, or the end of a session.`;
}

/** The session-count metric's sentence. Says "sessions" so it cannot be read as time. */
export function productiveSessionsHelperText(count: number): string {
  const sessions = `${count} completed session${count === 1 ? "" : "s"}`;
  return count === 0
    ? "No completed sessions in a row have averaged 70 or better yet."
    : `${sessions} in a row averaged 70 or better. This counts sessions, not time.`;
}
