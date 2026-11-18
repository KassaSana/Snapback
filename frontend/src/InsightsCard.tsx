import { memo, useMemo } from "react";

import type { SessionSummary } from "./api";
import { ChartDataTable } from "./ChartDataTable";
import {
  computeInsightsAggregates,
  focusBarHeightPct,
  toChronological,
} from "./insightsMetrics";

type InsightsCardProps = {
  rangeLabel: string;
  sessionHistory: SessionSummary[];
};

// SVG coordinate space; the element scales to its container via CSS width.
const CHART = { w: 480, h: 150, padX: 26, padTop: 26, padBottom: 24, maxBarW: 40 };

export function Tile({ value, label, detail }: { value: string; label: string; detail?: string }) {
  return (
    <div className="insight-tile">
      <p className="insight-tile-value">{value}</p>
      <p className="insight-tile-label">{label}</p>
      {detail ? <p className="meta-sub">{detail}</p> : null}
    </div>
  );
}

function FocusTrendChart({ summaries }: { summaries: SessionSummary[] }) {
  const { w, h, padX, padTop, padBottom, maxBarW } = CHART;
  const baseline = h - padBottom;
  const plotH = baseline - padTop;
  const plotW = w - padX * 2;
  const slot = plotW / summaries.length;
  const barW = Math.min(maxBarW, slot * 0.8);
  const midY = baseline - 0.5 * plotH;

  return (
    <svg
      className="insights-chart"
      viewBox={`0 0 ${w} ${h}`}
      role="img"
      aria-label="Average focus score by session, oldest to newest; data table below"
    >
      {/* Recessive reference lines: baseline (0) and a dashed midline (50). */}
      <line x1={padX} y1={baseline} x2={w - padX} y2={baseline} className="chart-baseline" />
      <line x1={padX} y1={midY} x2={w - padX} y2={midY} className="chart-midline" />
      <line x1={padX} y1={padTop} x2={w - padX} y2={padTop} className="chart-midline" />
      {[0, 50, 100].map((score) => (
        <text
          key={score}
          x="0"
          y={baseline - (score / 100) * plotH + 3}
          className="chart-axis-label"
        >
          {score}
        </text>
      ))}
      <text x={padX} y="146" className="chart-axis-label">
        Oldest
      </text>
      <text x={w - padX} y="146" textAnchor="end" className="chart-axis-label">
        Newest
      </text>
      {summaries.map((summary, index) => {
        const barH = (focusBarHeightPct(summary.recap.avgFocusScore) / 100) * plotH;
        const x = padX + index * slot + (slot - barW) / 2;
        const y = baseline - barH;
        const score = Math.round(summary.recap.avgFocusScore);
        return (
          <rect
            key={summary.record.sessionId || index}
            x={x}
            y={y}
            width={barW}
            height={barH}
            rx={Math.min(2, barW / 2)}
            className="chart-bar"
          >
            <title>{`${summary.recap.goal || "Session"} · focus ${score}`}</title>
          </rect>
        );
      })}
    </svg>
  );
}

// Two-step confirm: there is no undo.
export const InsightsCard = memo(function InsightsCard({
  rangeLabel,
  sessionHistory,
  truncationNote = null,
}: Pick<InsightsCardProps, "rangeLabel" | "sessionHistory"> & {
  /**
   * Set when the session list behind this chart hit the backend's cap, so
   * "All time" over the chart cannot quietly mean "the latest 500". Comes from the summary
   * report, which reads the same capped list.
   */
  truncationNote?: string | null;
}) {
  const aggregates = useMemo(() => computeInsightsAggregates(sessionHistory), [sessionHistory]);
  const chronological = useMemo(() => toChronological(sessionHistory), [sessionHistory]);
  return (
    <section className="card insights-card session-chart-card">
      <div className="card-header">
        <h2>Focus per session</h2>
        <span className="review-period">{rangeLabel}</span>
      </div>
      {sessionHistory.length === 0 ? (
        <p className="helper-text">
          No completed sessions yet. Finish a session to see your focus trends here.
        </p>
      ) : (
        <>
          <FocusTrendChart summaries={chronological} />
          <p className="insights-caption">
            Average focus score per session, 0 to 100, oldest first
            {truncationNote ? ` · ${truncationNote}` : ""}
          </p>
          <ChartDataTable
            caption="Average focus by session, oldest to newest"
            nameHeader="Session"
            rows={chronological.map((summary, index) => ({
              key: summary.record.sessionId || String(index),
              name: summary.recap.goal || "Session",
              detail: `focus ${Math.round(summary.recap.avgFocusScore)}`,
            }))}
          />
          <p className="helper-text">
            {Math.round(aggregates.avgDeepFocusPct)}% deep focus · {aggregates.totalSnapbacks}{" "}
            snapbacks across {aggregates.sessionCount} completed sessions
          </p>
        </>
      )}
    </section>
  );
});
