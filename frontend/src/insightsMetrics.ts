// Pure derivations for the insights view — kept separate from React so the
// aggregation and chart geometry are unit-testable headlessly.

import type { SessionSummary } from "./api";

export type InsightsAggregates = {
  sessionCount: number;
  avgFocusScore: number;
  avgDeepFocusPct: number;
  totalSnapbacks: number;
};

/**
 * Headline "Avg focus" for Insights when Review shares one range (Roadmap 10.11).
 * Summary, Trends, and Recent Focus use sample-weighted backend aggregates; the naive mean
 * of per-session recap scores can disagree by a point from rounding alone. Prefer the
 * shared range aggregate when predictions exist; fall back to the session mean otherwise.
 */
export const resolveInsightsAvgFocus = (
  rangeAvgFocusScore: number | null | undefined,
  sessionMeanAvgFocus: number,
): number =>
  rangeAvgFocusScore != null && Number.isFinite(rangeAvgFocusScore)
    ? rangeAvgFocusScore
    : sessionMeanAvgFocus;

/** Headline stat-tile numbers across the given sessions. */
export const computeInsightsAggregates = (
  summaries: SessionSummary[],
): InsightsAggregates => {
  if (summaries.length === 0) {
    return { sessionCount: 0, avgFocusScore: 0, avgDeepFocusPct: 0, totalSnapbacks: 0 };
  }
  const n = summaries.length;
  const sum = (pick: (s: SessionSummary) => number) =>
    summaries.reduce((acc, s) => acc + pick(s), 0);
  return {
    sessionCount: n,
    avgFocusScore: sum((s) => s.recap.avgFocusScore) / n,
    avgDeepFocusPct: sum((s) => s.recap.deepFocusPct) / n,
    totalSnapbacks: sum((s) => s.recap.snapbackCount),
  };
};

/**
 * `get_session_history` returns newest-first; the trend chart reads left→right
 * as oldest→newest, so reverse (without mutating the input).
 */
export const toChronological = (summaries: SessionSummary[]): SessionSummary[] =>
  [...summaries].reverse();

/** Bar height as a percent of the 0–100 focus-score domain, clamped. */
export const focusBarHeightPct = (avgFocusScore: number): number =>
  Math.max(0, Math.min(100, avgFocusScore));

/**
 * How a session is named in the delete list (Roadmap 7.6). Also the accessible name of its
 * delete button, so a destructive control never reads as a bare "Delete" — the user is
 * about to permanently remove one specific session and the button must say which.
 *
 * `recap.goal` and `record.goal` come from different queries and can disagree when a goal
 * was edited mid-session; the recap is the one the row's numbers describe, so it wins.
 */
export const sessionRowLabel = (summary: SessionSummary): string =>
  summary.recap.goal?.trim() || summary.record.goal?.trim() || "Untitled session";
