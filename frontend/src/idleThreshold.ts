// Roadmap 7.23. The AFK threshold, as the UI needs to talk about it.
//
// Pure functions in their own module for the reason sessionStatus.ts is: the component suite
// cannot run on this machine (11.11), so logic reachable only through a rendered component is
// untested locally. Anything with a rule in it lives here, where `tsx` can reach it.
//
// The bounds mirror src/types.hpp's kMinIdleThresholdSecs / kMaxIdleThresholdSecs. The native
// side is the authority — it rejects out-of-range values with an error rather than clamping —
// and these exist so the UI can say why *before* making the call, not so it can decide.

export const MIN_IDLE_THRESHOLD_SECS = 30;
export const MAX_IDLE_THRESHOLD_SECS = 3600;
export const DEFAULT_IDLE_THRESHOLD_SECS = 300;

/** The choices offered in the picker. A free-text seconds field would be a worse question. */
export const IDLE_THRESHOLD_CHOICES = [60, 120, 300, 600, 1800] as const;

/**
 * Coerce whatever the native side sent into a usable threshold.
 *
 * Out-of-range and non-numeric both fall back to the default rather than being clamped: a
 * clamped value looks like a setting the user chose, and this one nobody chose.
 */
export function normalizeIdleThresholdSecs(value: unknown): number {
  const seconds = Number(value);
  if (!Number.isFinite(seconds)) return DEFAULT_IDLE_THRESHOLD_SECS;
  const whole = Math.trunc(seconds);
  if (whole < MIN_IDLE_THRESHOLD_SECS || whole > MAX_IDLE_THRESHOLD_SECS) {
    return DEFAULT_IDLE_THRESHOLD_SECS;
  }
  return whole;
}

/** "5 minutes", "90 seconds", "1 hour" — whichever unit reads without arithmetic. */
export function formatIdleThreshold(seconds: number): string {
  if (seconds % 3600 === 0) {
    const hours = seconds / 3600;
    return `${hours} ${hours === 1 ? "hour" : "hours"}`;
  }
  if (seconds % 60 === 0) {
    const minutes = seconds / 60;
    return `${minutes} ${minutes === 1 ? "minute" : "minutes"}`;
  }
  return `${seconds} ${seconds === 1 ? "second" : "seconds"}`;
}

/**
 * What the Settings row says under its label, so the setting explains its own consequence.
 *
 * The wording matters more than it looks: the threshold does not stop the session or discard
 * anything, it stops the *attended-time* clock. Saying "pauses your session" would describe a
 * feature that does not exist.
 */
export function idleThresholdHelperText(seconds: number): string {
  return `After ${formatIdleThreshold(seconds)} without keyboard or mouse input, a session stops counting attended time. Elapsed time keeps running.`;
}
