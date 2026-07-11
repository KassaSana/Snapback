// Pure derivations for the insights view — kept separate from React so the
// aggregation and chart geometry are unit-testable headlessly.

import type { SessionSummary } from "./api";

export type InsightsAggregates = {
  sessionCount: number;
  avgFocusScore: number;
  avgDeepFocusPct: number;
  totalSnapbacks: number;
};

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
