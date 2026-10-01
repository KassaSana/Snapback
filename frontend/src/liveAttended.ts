/** Rebaseline cadence while a session is accruing attended time — not 1 Hz IPC. */
export const ATTENDED_REBASELINE_MS = 60_000;

/**
 * Local display minutes from the last authoritative baseline.
 * Native open spans already count to "now"; this only fills the gap between refreshes.
 */
export function liveAttendedMins(
  baselineMins: number,
  baselineAtMs: number,
  nowMs: number,
  accruing: boolean,
): number {
  if (!accruing) return baselineMins;
  const elapsedMins = Math.floor((nowMs - baselineAtMs) / 60_000);
  return baselineMins + Math.max(0, elapsedMins);
}

/**
 * Compact "Today …" label. Under one measured minute while still accruing, say `<1m`
 * so the line does not look stuck at `0m`.
 */
export function formatLiveTodayMins(mins: number, accruing: boolean): string {
  if (mins < 1 && accruing) return "<1m";
  const hours = Math.floor(mins / 60);
  const rest = mins % 60;
  return hours > 0 ? `${hours}h ${rest}m` : `${rest}m`;
}
