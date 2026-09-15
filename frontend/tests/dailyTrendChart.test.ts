import assert from "node:assert/strict";

import type { DailySummaryDay } from "../src/api";
import {
  TREND_CHART,
  dayBars,
  mondayOf,
  trendHeadline,
  trendRangeDays,
  weekComparison,
  weekComparisonLine,
} from "../src/dailyTrendChart";

const day = (iso: string, overrides: Partial<DailySummaryDay> = {}): DailySummaryDay => ({
  day: iso,
  attendedSecs: 0,
  focusedSecs: 0,
  deepFocusSecs: 0,
  avgFocusScore: 0,
  sampleCount: 0,
  sessionCount: 0,
  snapbackCount: 0,
  ...overrides,
});

// --- dayBars: gap filling, ascending order, ending today -----------------------------------

{
  const today = "2026-08-09"; // a Sunday
  const geometry = dayBars(
    [
      day("2026-08-05", {
        attendedSecs: 7200,
        deepFocusSecs: 3600,
        sampleCount: 10,
        avgFocusScore: 70,
      }),
      day("2026-08-08", {
        attendedSecs: 3600,
        deepFocusSecs: 900,
        sampleCount: 4,
        avgFocusScore: 55,
      }),
    ],
    7,
    today,
  );
  assert.equal(geometry.bars.length, 7);
  assert.equal(geometry.daysWithData, 2);
  assert.equal(geometry.bars[0].day, "2026-08-03");
  assert.equal(geometry.bars[6].day, today);
  // Days the backend omitted are drawn as no-data ticks, not skipped.
  assert.equal(geometry.bars[1].hasData, false);
  assert.equal(geometry.bars[1].attendedHeight, TREND_CHART.emptyTickHeight);
  const wednesday = geometry.bars.find((bar) => bar.day === "2026-08-05");
  assert.ok(wednesday && wednesday.hasData);
  // Bars ascend left to right.
  assert.ok(geometry.bars[0].x < geometry.bars[6].x);
}

// --- dayBars: scale, measured zero versus no data ------------------------------------------

{
  const today = "2026-08-09";
  const geometry = dayBars(
    [
      // Attended two hours, zero measured deep work: the deep bar must be a visible
      // sliver, not nothing — nothing is reserved for days with no data at all.
      day("2026-08-09", {
        attendedSecs: 7200,
        deepFocusSecs: 0,
        sampleCount: 5,
        avgFocusScore: 40,
      }),
    ],
    7,
    today,
  );
  const measured = geometry.bars[6];
  assert.equal(measured.hasData, true);
  assert.equal(measured.deepHeight, TREND_CHART.zeroBarHeight);
  assert.ok(measured.attendedHeight > measured.deepHeight);
  // 7200s against the smallest 2h-covering ceiling (7200) fills the plot.
  assert.equal(measured.attendedHeight, TREND_CHART.plotHeight);
}

// --- dayBars: friendly axis ceiling and reference labels -----------------------------------

{
  const geometry = dayBars(
    [day("2026-08-09", { attendedSecs: 5 * 3600, sampleCount: 1 })],
    7,
    "2026-08-09",
  );
  // 5h of attended time rounds the axis up to 6h, and the top reference says so in h/m.
  const top = geometry.references[geometry.references.length - 1];
  assert.equal(top.secs, 6 * 3600);
  assert.equal(top.label, "6h");
  assert.equal(geometry.references[0].label, "0");
}

// --- dayBars: labels carry true units (10.13) ----------------------------------------------

{
  const geometry = dayBars(
    [
      day("2026-08-09", {
        attendedSecs: 2 * 3600 + 120,
        deepFocusSecs: 3900,
        sampleCount: 1,
        avgFocusScore: 63.4,
      }),
    ],
    7,
    "2026-08-09",
  );
  const label = geometry.bars[6].label;
  assert.ok(label.includes("attended 2h 2m"));
  assert.ok(label.includes("deep 1h 5m"));
  assert.ok(label.includes("avg focus 63"));
  assert.ok(label.includes("1 sample"));
  const empty = geometry.bars[0].label;
  assert.ok(empty.includes("no data"));
}

// --- mondayOf: ISO weeks, Monday start -----------------------------------------------------

assert.equal(mondayOf("2026-08-09"), "2026-08-03"); // Sunday belongs to the week behind it
assert.equal(mondayOf("2026-08-03"), "2026-08-03"); // Monday is its own week's start
assert.equal(mondayOf("2026-08-08"), "2026-08-03"); // Saturday too

// --- weekComparison: sums and the empty-last-week null -------------------------------------

{
  const today = "2026-08-06"; // a Thursday; this ISO week began Monday 2026-08-03
  const days = [
    day("2026-07-29", { attendedSecs: 3600, deepFocusSecs: 1800, sampleCount: 9 }), // last week
    day("2026-08-01", { attendedSecs: 1800, deepFocusSecs: 600, sampleCount: 4 }), // last week
    day("2026-08-04", { attendedSecs: 7200, deepFocusSecs: 3000, sampleCount: 20 }), // this week
  ];
  const comparison = weekComparison(days, today);
  assert.ok(comparison);
  assert.equal(comparison.thisWeekDeepSecs, 3000);
  assert.equal(comparison.lastWeekDeepSecs, 2400);
  assert.equal(comparison.thisWeekAttendedSecs, 7200);
  assert.equal(comparison.lastWeekAttendedSecs, 5400);
  const line = weekComparisonLine(comparison);
  assert.ok(line.includes("50m this week"));
  assert.ok(line.includes("40m last week"));

  // No last-week data: no comparison, rather than a triumph over an empty week.
  assert.equal(weekComparison([days[2]], today), null);
}

// --- trendHeadline: neutral wording, delta thresholds --------------------------------------

{
  const today = "2026-08-06";
  const lastWeek = day("2026-07-30", { attendedSecs: 3600, deepFocusSecs: 3600, sampleCount: 5 });

  const more = trendHeadline(
    [lastWeek, day("2026-08-04", { attendedSecs: 9000, deepFocusSecs: 5400, sampleCount: 9 })],
    today,
    "Last 7 days",
  );
  assert.equal(more, "1h 30m of deep work this week — about 30m more than last week.");

  const less = trendHeadline(
    [lastWeek, day("2026-08-04", { attendedSecs: 3600, deepFocusSecs: 1800, sampleCount: 9 })],
    today,
    "Last 7 days",
  );
  assert.ok(less.includes("less than last week"));
  // No guilt vocabulary, per the attended-targets voice.
  assert.ok(!/only|missed|streak/i.test(less));

  const same = trendHeadline(
    [
      lastWeek,
      day("2026-08-04", { attendedSecs: 3600, deepFocusSecs: 3600 + 300, sampleCount: 9 }),
    ],
    today,
    "Last 7 days",
  );
  assert.ok(same.includes("about the same as last week"));

  // No last week at all: a plain total over the range, no invented comparison.
  const total = trendHeadline(
    [day("2026-08-04", { attendedSecs: 3600, deepFocusSecs: 1200, sampleCount: 9 })],
    today,
    "Last 7 days",
  );
  assert.equal(total, "20m of deep work last 7 days.");
}

// --- trendRangeDays: presets and the clamped custom span -----------------------------------

{
  const today = "2026-08-09";
  assert.equal(trendRangeDays("today", [], today), 1);
  assert.equal(trendRangeDays("7d", [], today), 7);
  assert.equal(trendRangeDays("30d", [], today), 30);
  // Custom/all spans stretch from the oldest day through today...
  assert.equal(trendRangeDays("custom", [day("2026-07-31")], today), 10);
  // ...but never below a week, and never past the retention window.
  assert.equal(trendRangeDays("custom", [day("2026-08-08")], today), 7);
  assert.equal(trendRangeDays("all", [day("2026-01-01")], today), 90);
  assert.equal(trendRangeDays("all", [], today), 7);
}

console.log("dailyTrendChart.test.ts passed");
