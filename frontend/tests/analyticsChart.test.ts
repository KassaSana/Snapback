import assert from "node:assert/strict";

import {
  CHART,
  CHART_MAX_SCORE,
  contextSampleLabel,
  hourBars,
  referenceLines,
} from "../src/analyticsChart";
import type { AnalyticsHour } from "../src/api";

const hour = (
  h: number,
  avgFocusScore: number,
  sampleCount = 10,
  distractedFraction = 0,
): AnalyticsHour => ({ hour: h, avgFocusScore, sampleCount, distractedFraction });

const barAt = (bars: ReturnType<typeof hourBars>, h: number) => {
  const found = bars.find((bar) => bar.hour === h);
  assert.ok(found, `expected a bar for hour ${h}`);
  return found;
};

// Every hour of the day is represented, in order, whether or not it has data. A chart that
// only draws the hours it has is a chart with a shifting x-axis.
{
  const bars = hourBars([hour(9, 50)]);
  assert.equal(bars.length, 24);
  assert.deepEqual(
    bars.map((bar) => bar.hour),
    Array.from({ length: 24 }, (_, i) => i),
  );
}

// THE BUG. A dataset whose best hour is 20/100 must draw that hour at a fifth of the plot,
// not at full height. Under the old max-relative scaling this was 100 -- the chart told the
// user their worst day was their peak.
{
  const bars = hourBars([hour(9, 20), hour(10, 10)]);
  assert.equal(barAt(bars, 9).height, (20 / CHART_MAX_SCORE) * CHART.plotHeight);
  assert.equal(barAt(bars, 10).height, (10 / CHART_MAX_SCORE) * CHART.plotHeight);
  // And the relationship between them is preserved: half the score, half the bar.
  assert.equal(barAt(bars, 9).height, barAt(bars, 10).height * 2);
}

// A perfect hour reaches the top of the plot and no further.
{
  const bars = hourBars([hour(9, 100)]);
  assert.equal(barAt(bars, 9).height, CHART.plotHeight);
  assert.equal(barAt(bars, 9).y, CHART.baselineY - CHART.plotHeight);
}

// Out-of-range values are clamped rather than allowed to overflow the plot, which would make
// every other bar misleading by comparison.
{
  const bars = hourBars([hour(9, 250), hour(10, -30)]);
  assert.equal(barAt(bars, 9).height, CHART.plotHeight);
  assert.equal(barAt(bars, 10).height, CHART.zeroBarHeight);
}

// A measured zero and a missing hour are different pictures. This is the second half of the
// bug: "you were completely distracted" and "we have no idea" used to render identically.
{
  const bars = hourBars([hour(9, 0, 40, 1)]);
  const measuredZero = barAt(bars, 9);
  const missing = barAt(bars, 10);

  assert.equal(measuredZero.hasData, true);
  assert.equal(measuredZero.height, CHART.zeroBarHeight); // visible, not nothing
  assert.ok(measuredZero.label.includes("focus 0 of 100"));
  assert.ok(measuredZero.label.includes("40 samples"));
  assert.ok(measuredZero.label.includes("100% distracted"));

  assert.equal(missing.hasData, false);
  assert.equal(missing.label, "10:00 · no data");
  assert.notEqual(measuredZero.hasData, missing.hasData);
}

// An hour reported with zero samples is no data, not a measured zero -- the count is what
// says whether anything was observed, not the score.
{
  const bars = hourBars([hour(9, 0, 0)]);
  assert.equal(barAt(bars, 9).hasData, false);
}

// The label carries what the bar's height cannot: how much it is based on, and how much of it
// was distraction. A single bar of average focus hides both.
{
  const bars = hourBars([hour(14, 72.4, 1, 0.253)]);
  assert.equal(barAt(bars, 14).label, "14:00 · focus 72 of 100 · 1 sample · 25% distracted");
}

// Fixed references at 0, 50, and 100, with 0 on the baseline and 100 a full plot above it.
{
  const lines = referenceLines();
  assert.deepEqual(
    lines.map((line) => line.score),
    [0, 50, 100],
  );
  assert.equal(lines[0].y, CHART.baselineY);
  assert.equal(lines[2].y, CHART.baselineY - CHART.plotHeight);
  assert.equal(lines[1].y, CHART.baselineY - CHART.plotHeight / 2);
}

// Bars are laid out on a fixed hourly pitch, so hour 0 and hour 23 sit where the axis says.
{
  const bars = hourBars([]);
  assert.equal(barAt(bars, 0).x, CHART.firstBarX);
  assert.equal(barAt(bars, 23).x, CHART.firstBarX + 23 * CHART.hourSpacing);
}

// The app metric counts periodic context snapshots, not switches. Naming them "switches"
// credited anyone who sat still in one window with more app-hopping the longer they stayed.
assert.equal(contextSampleLabel(12), "12 samples");
assert.equal(contextSampleLabel(1), "1 sample");
assert.ok(!contextSampleLabel(12).includes("switch"));

console.log("analyticsChart.test.ts passed");
