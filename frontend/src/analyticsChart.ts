// Roadmap 10.8. The geometry and wording of the hourly focus chart, as pure functions.
//
// Split out of AnalyticsCard for the reason sessionStatus.ts and idleThreshold.ts were: the
// component suite cannot run on this machine (11.11), so anything reachable only through a
// rendered component is untested locally. Chart geometry is exactly the kind of thing that
// looks right and is wrong.
//
// The bug this replaces: every bar was drawn as a fraction of the *largest value in the
// current dataset*. A user whose best hour scored 20/100 saw that hour at full height, so the
// chart said "this is your peak" where the number said "this is poor" — and the two were the
// same measurement. A missing hour was drawn identically to a measured zero, which made "we
// have no idea" and "you were completely distracted" the same picture.

import type { AnalyticsHour } from "./api";

/** Focus scores are a 0-100 scale, so the axis is too. This is the whole fix. */
export const CHART_MAX_SCORE = 100;

/** Geometry of the 480x150 viewBox the card renders. */
export const CHART = {
  baselineY: 126,
  plotHeight: 100,
  barWidth: 12,
  firstBarX: 26,
  hourSpacing: 18,
  /**
   * A measured zero still gets a visible sliver. Zero height would draw nothing, which is the
   * one thing a *measured* zero must not look like — that picture is reserved for no data.
   */
  zeroBarHeight: 2,
  /** The no-data mark: a short tick under the axis, so it cannot be mistaken for a bar. */
  emptyTickHeight: 3,
} as const;

export type HourBar = {
  hour: number;
  x: number;
  y: number;
  height: number;
  /** False when the hour has no samples at all — drawn as a tick below the axis. */
  hasData: boolean;
  /** Accessible/tooltip text. Carries the sample count and distraction rate, not just a score. */
  label: string;
};

/** Where a reference line sits, for the fixed 0/50/100 axis. */
export type ReferenceLine = { score: number; y: number };

export const REFERENCE_SCORES = [0, 50, 100] as const;

export function referenceLines(): ReferenceLine[] {
  return REFERENCE_SCORES.map((score) => ({
    score,
    y: CHART.baselineY - (score / CHART_MAX_SCORE) * CHART.plotHeight,
  }));
}

function formatHour(hour: number): string {
  return `${String(hour).padStart(2, "0")}:00`;
}

/**
 * One entry per hour of the day, in order, whether or not that hour has data.
 *
 * Scores outside 0-100 are clamped rather than dropped: a bar taller than the axis would
 * silently overflow the plot and misreport every other bar by comparison.
 */
export function hourBars(hourly: readonly AnalyticsHour[]): HourBar[] {
  const byHour = new Map(hourly.map((entry) => [entry.hour, entry]));
  return Array.from({ length: 24 }, (_, hour) => {
    const entry = byHour.get(hour);
    const x = CHART.firstBarX + hour * CHART.hourSpacing;
    if (!entry || entry.sampleCount === 0) {
      return {
        hour,
        x,
        y: CHART.baselineY,
        height: CHART.emptyTickHeight,
        hasData: false,
        label: `${formatHour(hour)} · no data`,
      };
    }
    const score = Math.min(CHART_MAX_SCORE, Math.max(0, entry.avgFocusScore));
    const scaled = (score / CHART_MAX_SCORE) * CHART.plotHeight;
    const height = scaled === 0 ? CHART.zeroBarHeight : scaled;
    return {
      hour,
      x,
      y: CHART.baselineY - height,
      height,
      hasData: true,
      label:
        `${formatHour(hour)} · focus ${Math.round(score)} of 100 · ` +
        `${entry.sampleCount} ${entry.sampleCount === 1 ? "sample" : "samples"} · ` +
        `${Math.round(Math.min(1, Math.max(0, entry.distractedFraction)) * 100)}% distracted`,
    };
  });
}

/**
 * How the top-apps list describes its numbers.
 *
 * `context_app_counts` counts context snapshot rows, which the engine writes periodically as
 * well as on a real window change. Calling them "switches" therefore overstated app-hopping
 * for anyone who sat in one window — the longer you stayed put, the more "switches" you were
 * credited with. Roadmap 10.8 is explicit that the SQL count must not be renamed by wish, so
 * the label describes what is actually counted and a real switch/dwell metric stays a separate
 * question.
 */
export function contextSampleLabel(count: number): string {
  return `${count} ${count === 1 ? "sample" : "samples"}`;
}
