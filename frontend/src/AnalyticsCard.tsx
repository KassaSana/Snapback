import { memo } from "react";

import type { AnalyticsSummary, AppRuleKind, AppRuleRecord } from "./api";
import {
  CHART,
  CHART_MAX_SCORE,
  contextSampleLabel,
  hourBars,
  referenceLines,
} from "./analyticsChart";
import { ChartDataTable } from "./ChartDataTable";
import { productiveSessionsHelperText } from "./focusStreak";
import { getAppRuleForName, ruleKindLabel } from "./useAppRules";

type AnalyticsCardProps = {
  analytics: AnalyticsSummary;
  appRules?: AppRuleRecord[];
  onCreateAppRule?: (appName: string, kind: AppRuleKind) => void | Promise<void>;
  rangeLabel: string;
};

export const AnalyticsCard = memo(function AnalyticsCard({
  analytics,
  appRules,
  onCreateAppRule,
  rangeLabel,
}: AnalyticsCardProps) {
  // Roadmap 10.8. Geometry lives in analyticsChart.ts so it can be tested without a DOM;
  // this component only places what it is given.
  const bars = hourBars(analytics.hourly);
  const references = referenceLines();

  return (
    <>
      <section className="card insights-card analytics-card">
        <div className="card-header">
          <h2>Focus by hour</h2>
          <span className="pill">{rangeLabel}</span>
        </div>
        {analytics.sampleCount === 0 ? (
          <p className="helper-text">No prediction data yet. Start a session to build trends.</p>
        ) : (
          <>
            <svg
              className="insights-chart"
              viewBox="0 0 480 150"
              role="img"
              aria-label={`Average focus by hour of day, on a fixed 0 to ${CHART_MAX_SCORE} scale; data table below`}
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
              {[0, 6, 12, 18, 23].map((hour) => (
                <text
                  key={hour}
                  x={CHART.firstBarX + hour * CHART.hourSpacing + CHART.barWidth / 2}
                  y="146"
                  textAnchor="middle"
                  className="chart-axis-label"
                >
                  {String(hour).padStart(2, "0")}
                </text>
              ))}
            </svg>
            <p className="insights-caption">
              Average focus by hour of day, 0–100. Ticks below the line are hours with no data.
            </p>
            <ChartDataTable
              caption={`Focus by hour, ${rangeLabel}`}
              nameHeader="Hour"
              rows={bars.map((bar) => ({
                key: String(bar.hour),
                name: bar.name,
                detail: bar.detail,
              }))}
            />
            <p className="helper-text">
              {analytics.sampleCount} prediction samples ·{" "}
              {productiveSessionsHelperText(analytics.productiveSessionStreak)}
            </p>
          </>
        )}
      </section>
      <section className="card top-apps-card">
        <div className="card-header">
          <h2>Top apps</h2>
          <span className="pill">{rangeLabel}</span>
        </div>
        <ul className="history-list">
          {analytics.topApps.length === 0 ? (
            <li className="history-empty">No app context data yet.</li>
          ) : (
            analytics.topApps.map((app) => {
              const rule = appRules ? getAppRuleForName(appRules, app.appName) : undefined;
              return (
                <li key={app.appName} className="history-item top-app-item">
                  <div className="top-app-info">
                    <span className="top-app-name">{app.appName}</span>
                    {rule ? (
                      <span className={`rules-badge rules-badge-${rule.ruleType}`}>
                        {ruleKindLabel(rule.ruleType)}
                      </span>
                    ) : (
                      onCreateAppRule && (
                        <div className="timeline-quick-rules">
                          <button
                            type="button"
                            className="mini-action-button allow-btn"
                            title={`Always treat "${app.appName}" as Productive`}
                            onClick={() => void onCreateAppRule(app.appName, "allow")}
                          >
                            + Allow
                          </button>
                          <button
                            type="button"
                            className="mini-action-button block-btn"
                            title={`Always treat "${app.appName}" as Distracting`}
                            onClick={() => void onCreateAppRule(app.appName, "block")}
                          >
                            + Block
                          </button>
                        </div>
                      )
                    )}
                  </div>
                  <strong>{contextSampleLabel(app.windowCount)}</strong>
                </li>
              );
            })
          )}
        </ul>

        <p className="insights-caption">
          Context samples are periodic observations of the focused window, not app switches.
        </p>
      </section>
    </>
  );
});
