import { memo } from "react";

import type { AnalyticsSummary } from "./api";
import {
  CHART,
  CHART_MAX_SCORE,
  contextSampleLabel,
  hourBars,
  referenceLines,
} from "./analyticsChart";

type AnalyticsCardProps = { analytics: AnalyticsSummary };

export const AnalyticsCard = memo(function AnalyticsCard({ analytics }: AnalyticsCardProps) {
  // Roadmap 10.8. Geometry lives in analyticsChart.ts so it can be tested without a DOM;
  // this component only places what it is given.
  const bars = hourBars(analytics.hourly);
  const references = referenceLines();

  return (
    <section className="card insights-card analytics-card">
      <div className="card-header">
        <h2>Trends</h2>
        <span className="pill">all recorded sessions</span>
      </div>
      {analytics.sampleCount === 0 ? (
        <p className="helper-text">No prediction data yet. Start a session to build trends.</p>
      ) : (
        <>
          <div className="insight-tiles">
            <div className="insight-tile"><p className="insight-tile-value">{Math.round(analytics.avgFocusScore)}</p><p className="insight-tile-label">Avg focus</p></div>
            <div className="insight-tile"><p className="insight-tile-value">{analytics.sampleCount}</p><p className="insight-tile-label">Samples</p></div>
            <div className="insight-tile"><p className="insight-tile-value">{analytics.productiveSessionStreak}</p><p className="insight-tile-label">Focus streak</p></div>
          </div>
          <svg
            className="insights-chart"
            viewBox="0 0 480 150"
            role="img"
            aria-label={`Average focus by hour of day, on a fixed 0 to ${CHART_MAX_SCORE} scale`}
          >
            {references.map((reference) => (
              <g key={reference.score}>
                <line
                  x1="24"
                  y1={reference.y}
                  x2="468"
                  y2={reference.y}
                  className={reference.score === 0 ? "chart-baseline" : "chart-midline"}
                />
                <text x="2" y={reference.y + 3} className="chart-axis-label">
                  {reference.score}
                </text>
              </g>
            ))}
            {bars.map((bar) =>
              bar.hasData ? (
                <rect
                  key={bar.hour}
                  x={bar.x}
                  y={bar.y}
                  width={CHART.barWidth}
                  height={bar.height}
                  className="chart-bar"
                >
                  <title>{bar.label}</title>
                </rect>
              ) : (
                // Below the axis, in its own class: "we measured nothing here" must not be
                // drawable as "we measured zero here".
                <rect
                  key={bar.hour}
                  x={bar.x}
                  y={CHART.baselineY + 2}
                  width={CHART.barWidth}
                  height={bar.height}
                  className="chart-bar-empty"
                >
                  <title>{bar.label}</title>
                </rect>
              ),
            )}
          </svg>
          <p className="insights-caption">
            Average focus by hour of day, 0–100. Ticks below the line are hours with no data.
          </p>
          <ul className="history-list">
            {analytics.topApps.length === 0 ? <li className="history-empty">No app context data yet.</li> : analytics.topApps.map((app) => (
              <li key={app.appName} className="history-item"><span>{app.appName}</span><strong>{contextSampleLabel(app.windowCount)}</strong></li>
            ))}
          </ul>
          <p className="insights-caption">
            Context samples are periodic observations of the focused window, not app switches.
          </p>
        </>
      )}
    </section>
  );
});
