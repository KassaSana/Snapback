import { memo } from "react";

import type { DailySummary } from "./api";
import type { ReviewRangePreset } from "./reviewRange";
import {
  TREND_CHART,
  dayBars,
  localIsoDay,
  trendHeadline,
  trendRangeDays,
  weekComparison,
  weekComparisonLine,
} from "./dailyTrendChart";

type DailyTrendCardProps = {
  dailySummary: DailySummary;
  rangePreset: ReviewRangePreset;
  rangeLabel: string;
};

export const DailyTrendCard = memo(function DailyTrendCard({
  dailySummary,
  rangePreset,
  rangeLabel,
}: DailyTrendCardProps) {
  // Geometry lives in dailyTrendChart.ts so it can be tested without a DOM; this component
  // only places what it is given (the analyticsChart split, applied again).
  const todayIso = localIsoDay();
  const { days, capped } = dailySummary;
  const comparison = weekComparison(days, todayIso);
  const rangeDays = trendRangeDays(rangePreset, days, todayIso);
  const { bars, references, daysWithData } = dayBars(days, rangeDays, todayIso);
  // One bar is not a trend: for a single-day range the headline and week strip carry the
  // card, and the chart waits for a wider range.
  const showChart = rangeDays > 1;
  // Sparse x-labels past a week, so thirty slots do not become thirty labels.
  const labelEvery = rangeDays <= 7 ? 1 : 5;

  return (
    <section className="card insights-card daily-trend-card">
      <div className="card-header">
        <h2>Deep work</h2>
        <span className="pill">{rangeLabel}</span>
      </div>
      {days.length === 0 ? (
        <p className="helper-text">
          No deep work measured yet. Start a session to build the trend.
        </p>
      ) : (
        <>
          <p className="trend-headline">{trendHeadline(days, todayIso, rangeLabel)}</p>
          {comparison ? <p className="helper-text">{weekComparisonLine(comparison)}</p> : null}
          {showChart ? (
            <>
              <svg
                className="insights-chart"
                viewBox="0 0 480 150"
                role="img"
                aria-label={`Deep work per day inside attended time, last ${rangeDays} days`}
              >
                {references.map((reference) => (
                  <g key={reference.secs}>
                    <line
                      x1="24"
                      y1={reference.y}
                      x2="470"
                      y2={reference.y}
                      className={reference.secs === 0 ? "chart-baseline" : "chart-midline"}
                    />
                    <text x="2" y={reference.y + 3} className="chart-axis-label">
                      {reference.label}
                    </text>
                  </g>
                ))}
                {bars.map((bar, index) =>
                  bar.hasData ? (
                    <g key={bar.day}>
                      <rect
                        x={bar.x}
                        y={bar.attendedY}
                        width={bar.barWidth}
                        height={bar.attendedHeight}
                        className="chart-bar-context"
                      >
                        <title>{bar.label}</title>
                      </rect>
                      <rect
                        x={bar.x}
                        y={bar.deepY}
                        width={bar.barWidth}
                        height={bar.deepHeight}
                        className="chart-bar"
                      >
                        <title>{bar.label}</title>
                      </rect>
                      {index % labelEvery === 0 ? (
                        <text
                          x={bar.x + bar.barWidth / 2}
                          y={TREND_CHART.baselineY + 12}
                          textAnchor="middle"
                          className="chart-axis-label"
                        >
                          {bar.weekdayInitial}
                        </text>
                      ) : null}
                    </g>
                  ) : (
                    // Below the axis, in its own class: "we measured nothing here" must not
                    // be drawable as "we measured zero here".
                    <g key={bar.day}>
                      <rect
                        x={bar.x}
                        y={TREND_CHART.baselineY + 2}
                        width={bar.barWidth}
                        height={bar.attendedHeight}
                        className="chart-bar-empty"
                      >
                        <title>{bar.label}</title>
                      </rect>
                      {index % labelEvery === 0 ? (
                        <text
                          x={bar.x + bar.barWidth / 2}
                          y={TREND_CHART.baselineY + 12}
                          textAnchor="middle"
                          className="chart-axis-label"
                        >
                          {bar.weekdayInitial}
                        </text>
                      ) : null}
                    </g>
                  ),
                )}
              </svg>
              <p className="insights-caption">
                Deep work per day (solid) inside attended time (light). Ticks below the line are
                days with nothing recorded.
              </p>
            </>
          ) : null}
          {daysWithData < 3 && showChart ? (
            <p className="insights-caption">Trends fill in as you run more sessions.</p>
          ) : null}
          {capped ? (
            <p className="insights-caption">
              Showing the retention window — older days are no longer stored.
            </p>
          ) : null}
        </>
      )}
    </section>
  );
});
