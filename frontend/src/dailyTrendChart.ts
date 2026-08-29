// Geometry and wording of the daily deep-work trend, as pure functions, for the same reason
// analyticsChart.ts exists: the component suite cannot run everywhere, and chart geometry is
// exactly the kind of thing that looks right and is wrong.
//
// The two rules inherited from that module: a *measured* zero is a visible sliver and "no
// data" is a tick below the axis, never the same picture; and every label carries its true
// unit (10.13) — the *Secs fields are durations, and the sample count is named as a count.
//
// The axis is minutes-shaped, not 0-100: unlike focus scores, daily attended time has no
// natural maximum, so the scale is the range's largest day rounded up to a friendly step.
// That is honest here because the bars are all the same measurement on the same scale —
// the bug the fixed 0-100 axis fixed was *different* measurements normalized to the best one.

import type { DailySummaryDay } from "./api";
import { formatFocusStretch } from "./focusStreak";

/** Geometry of the 480x150 viewBox the card renders, matching the hourly chart's frame. */
export const TREND_CHART = {
  baselineY: 126,
  plotHeight: 100,
  plotLeft: 26,
  plotRight: 470,
  zeroBarHeight: 2,
  emptyTickHeight: 3,
  /** Gap between day slots, as a fraction of the slot width. */
  gapFraction: 0.25,
} as const;

/** The friendly y-axis ceilings, in seconds. The scale is the first that fits the data. */
const AXIS_STEPS_SECS = [
  1800,
  3600,
  2 * 3600,
  3 * 3600,
  4 * 3600,
  6 * 3600,
  8 * 3600,
  10 * 3600,
  12 * 3600,
  16 * 3600,
  24 * 3600,
] as const;

const WEEKDAY_INITIALS = ["S", "M", "T", "W", "T", "F", "S"] as const;
const MONTHS = [
  "Jan",
  "Feb",
  "Mar",
  "Apr",
  "May",
  "Jun",
  "Jul",
  "Aug",
  "Sep",
  "Oct",
  "Nov",
  "Dec",
] as const;

export type DayBar = {
  /** Local calendar date, "YYYY-MM-DD". */
  day: string;
  x: number;
  barWidth: number;
  /** The lighter context bar: attended time. */
  attendedY: number;
  attendedHeight: number;
  /** The primary bar: deep-work time, drawn over the attended bar. */
  deepY: number;
  deepHeight: number;
  /** False when the day has nothing recorded — drawn as a tick below the axis. */
  hasData: boolean;
  weekdayInitial: string;
  /** Accessible/tooltip text. Durations in h/m, the sample count named as samples. */
  label: string;
};

export type TrendReference = { secs: number; y: number; label: string };

export type DailyTrendGeometry = {
  bars: DayBar[];
  references: TrendReference[];
  /** How many of the drawn days actually have data. */
  daysWithData: number;
};

/** Today's local calendar date as "YYYY-MM-DD" — the same bucketing key the backend uses. */
export function localIsoDay(date: Date = new Date()): string {
  const month = String(date.getMonth() + 1).padStart(2, "0");
  const dayOfMonth = String(date.getDate()).padStart(2, "0");
  return `${date.getFullYear()}-${month}-${dayOfMonth}`;
}

/** Parse "YYYY-MM-DD" as a *local* date. `new Date(string)` would read it as UTC midnight,
 *  which shifts the day for every user west of Greenwich. */
function parseLocalDay(day: string): Date {
  const [year, month, dayOfMonth] = day.split("-").map(Number);
  return new Date(year || 1970, (month || 1) - 1, dayOfMonth || 1);
}

function shiftDay(day: string, byDays: number): string {
  const date = parseLocalDay(day);
  date.setDate(date.getDate() + byDays);
  return localIsoDay(date);
}

function friendlyCeiling(maxSecs: number): number {
  for (const step of AXIS_STEPS_SECS) {
    if (maxSecs <= step) return step;
  }
  // Past the largest step, round up to the next two hours; nobody attends 24h+, but a
  // clock-skewed span must widen the axis rather than overflow the plot.
  return Math.ceil(maxSecs / (2 * 3600)) * 2 * 3600;
}

function dayLabel(day: string): string {
  const date = parseLocalDay(day);
  return `${date.toLocaleDateString(undefined, { weekday: "short" })} ${
    MONTHS[date.getMonth()]
  } ${date.getDate()}`;
}

/**
 * One entry per calendar day, oldest first, ending at `todayIso` — whether or not the
 * backend sent that day. The backend omits empty days; the chart draws them as gaps so a
 * skipped day stays visible as a day, not silently compressed out of the week.
 */
export function dayBars(
  days: readonly DailySummaryDay[],
  rangeDays: number,
  todayIso: string,
): DailyTrendGeometry {
  const count = Math.max(1, Math.floor(rangeDays));
  const byDay = new Map(days.map((entry) => [entry.day, entry]));

  const plotWidth = TREND_CHART.plotRight - TREND_CHART.plotLeft;
  const slotWidth = plotWidth / count;
  const barWidth = Math.max(2, slotWidth * (1 - TREND_CHART.gapFraction));

  const drawn: Array<DailySummaryDay | null> = [];
  const dayKeys: string[] = [];
  for (let back = count - 1; back >= 0; back -= 1) {
    const key = shiftDay(todayIso, -back);
    dayKeys.push(key);
    drawn.push(byDay.get(key) ?? null);
  }

  const maxSecs = drawn.reduce(
    (max, entry) =>
      entry ? Math.max(max, entry.attendedSecs, entry.focusedSecs, entry.deepFocusSecs) : max,
    0,
  );
  const ceiling = friendlyCeiling(Math.max(maxSecs, 1800));

  const scaled = (secs: number): number => {
    const clamped = Math.min(ceiling, Math.max(0, secs));
    const height = (clamped / ceiling) * TREND_CHART.plotHeight;
    return height === 0 ? TREND_CHART.zeroBarHeight : height;
  };

  let daysWithData = 0;
  const bars = dayKeys.map((key, index): DayBar => {
    const entry = drawn[index];
    const x = TREND_CHART.plotLeft + index * slotWidth + (slotWidth - barWidth) / 2;
    const weekdayInitial = WEEKDAY_INITIALS[parseLocalDay(key).getDay()];
    if (!entry) {
      return {
        day: key,
        x,
        barWidth,
        attendedY: TREND_CHART.baselineY,
        attendedHeight: TREND_CHART.emptyTickHeight,
        deepY: TREND_CHART.baselineY,
        deepHeight: 0,
        hasData: false,
        weekdayInitial,
        label: `${dayLabel(key)} · no data`,
      };
    }
    daysWithData += 1;
    const attendedHeight = scaled(entry.attendedSecs);
    const deepHeight = scaled(entry.deepFocusSecs);
    const samples = `${entry.sampleCount} ${entry.sampleCount === 1 ? "sample" : "samples"}`;
    return {
      day: key,
      x,
      barWidth,
      attendedY: TREND_CHART.baselineY - attendedHeight,
      attendedHeight,
      deepY: TREND_CHART.baselineY - deepHeight,
      deepHeight,
      hasData: true,
      weekdayInitial,
      label:
        `${dayLabel(key)} · attended ${formatFocusStretch(entry.attendedSecs)} · ` +
        `deep ${formatFocusStretch(entry.deepFocusSecs)} · ` +
        `avg focus ${Math.round(entry.avgFocusScore)} (${samples})`,
    };
  });

  const references: TrendReference[] = [0, ceiling / 2, ceiling].map((secs) => ({
    secs,
    y: TREND_CHART.baselineY - (secs / ceiling) * TREND_CHART.plotHeight,
    label: secs === 0 ? "0" : formatFocusStretch(secs),
  }));

  return { bars, references, daysWithData };
}

export type WeekComparison = {
  thisWeekDeepSecs: number;
  lastWeekDeepSecs: number;
  thisWeekAttendedSecs: number;
  lastWeekAttendedSecs: number;
};

/** The Monday on or before `day` — ISO weeks, the same rule as attended_secs_in_local_week. */
export function mondayOf(day: string): string {
  const date = parseLocalDay(day);
  const back = (date.getDay() + 6) % 7;
  return shiftDay(day, -back);
}

/**
 * This ISO week versus the one before it, or null when last week recorded nothing — a
 * comparison against an empty week would read as a triumph over a week that may simply
 * predate the install.
 */
export function weekComparison(
  days: readonly DailySummaryDay[],
  todayIso: string,
): WeekComparison | null {
  const thisMonday = mondayOf(todayIso);
  const lastMonday = shiftDay(thisMonday, -7);
  const sums: WeekComparison = {
    thisWeekDeepSecs: 0,
    lastWeekDeepSecs: 0,
    thisWeekAttendedSecs: 0,
    lastWeekAttendedSecs: 0,
  };
  let lastWeekHasData = false;
  for (const entry of days) {
    if (entry.day >= thisMonday && entry.day <= todayIso) {
      sums.thisWeekDeepSecs += entry.deepFocusSecs;
      sums.thisWeekAttendedSecs += entry.attendedSecs;
    } else if (entry.day >= lastMonday && entry.day < thisMonday) {
      sums.lastWeekDeepSecs += entry.deepFocusSecs;
      sums.lastWeekAttendedSecs += entry.attendedSecs;
      if (entry.attendedSecs > 0 || entry.sampleCount > 0) lastWeekHasData = true;
    }
  }
  return lastWeekHasData ? sums : null;
}

/** Differences under ten minutes are noise, and the copy says so instead of implying rank. */
const SAME_DELTA_SECS = 600;

/**
 * The card's one opening sentence. Neutral by rule: a measurement and at most one
 * comparison — no streaks, no "only", no encouragement (the AttendedTargets voice).
 */
export function trendHeadline(
  days: readonly DailySummaryDay[],
  todayIso: string,
  rangeLabel: string,
): string {
  const comparison = weekComparison(days, todayIso);
  if (comparison) {
    const deep = formatFocusStretch(comparison.thisWeekDeepSecs);
    const delta = comparison.thisWeekDeepSecs - comparison.lastWeekDeepSecs;
    if (Math.abs(delta) < SAME_DELTA_SECS) {
      return `${deep} of deep work this week — about the same as last week.`;
    }
    const direction = delta > 0 ? "more" : "less";
    return `${deep} of deep work this week — about ${formatFocusStretch(
      Math.abs(delta),
    )} ${direction} than last week.`;
  }
  const totalDeep = days.reduce((sum, entry) => sum + entry.deepFocusSecs, 0);
  return `${formatFocusStretch(totalDeep)} of deep work ${rangeLabel.toLowerCase()}.`;
}

/** The comparison strip under the headline, when there is a last week to compare against. */
export function weekComparisonLine(comparison: WeekComparison): string {
  return (
    `Deep work: ${formatFocusStretch(comparison.thisWeekDeepSecs)} this week · ` +
    `${formatFocusStretch(comparison.lastWeekDeepSecs)} last week. ` +
    `Attended: ${formatFocusStretch(comparison.thisWeekAttendedSecs)} · ` +
    `${formatFocusStretch(comparison.lastWeekAttendedSecs)}.`
  );
}

/** How many day slots the chart draws for a Review range. */
export function trendRangeDays(
  preset: "today" | "7d" | "30d" | "all" | "custom",
  days: readonly DailySummaryDay[],
  todayIso: string,
): number {
  if (preset === "today") return 1;
  if (preset === "7d") return 7;
  if (preset === "30d") return 30;
  // "all" and custom: span from the oldest returned day through today, kept between a week
  // and the retention window so one stray old row cannot flatten the chart into noise.
  const oldest = days.reduce((min, entry) => (min === "" || entry.day < min ? entry.day : min), "");
  if (oldest === "") return 7;
  const spanMs = parseLocalDay(todayIso).getTime() - parseLocalDay(oldest).getTime();
  const spanDays = Math.round(spanMs / (24 * 3600 * 1000)) + 1;
  return Math.min(90, Math.max(7, spanDays));
}
