import { useMemo } from "react";

import type { SessionSummary } from "./api";
import {
  computeInsightsAggregates,
  focusBarHeightPct,
  toChronological,
} from "./insightsMetrics";

type InsightsCardProps = {
  sessionHistory: SessionSummary[];
};

// SVG coordinate space; the element scales to its container via CSS width.
const CHART = { w: 320, h: 120, padX: 4, padTop: 8, padBottom: 6, gap: 3, maxBarW: 40 };

export function Tile({ value, label }: { value: string; label: string }) {
  return (
    <div className="insight-tile">
      <p className="insight-tile-value">{value}</p>
      <p className="insight-tile-label">{label}</p>
    </div>
  );
}

function FocusTrendChart({ summaries }: { summaries: SessionSummary[] }) {
  const { w, h, padX, padTop, padBottom, gap, maxBarW } = CHART;
  const baseline = h - padBottom;
  const plotH = baseline - padTop;
  const plotW = w - padX * 2;
  const slot = plotW / summaries.length;
  const barW = Math.min(maxBarW, Math.max(2, slot - gap));
  const midY = baseline - 0.5 * plotH;

  return (
    <svg
      className="insights-chart"
      viewBox={`0 0 ${w} ${h}`}
      role="img"
      aria-label="Average focus score by session, oldest to newest"
    >
      {/* Recessive reference lines: baseline (0) and a dashed midline (50). */}
      <line x1={padX} y1={baseline} x2={w - padX} y2={baseline} className="chart-baseline" />
      <line x1={padX} y1={midY} x2={w - padX} y2={midY} className="chart-midline" />
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

export function InsightsCard({ sessionHistory }: InsightsCardProps) {
  const aggregates = useMemo(
    () => computeInsightsAggregates(sessionHistory),
    [sessionHistory],
  );
  const chronological = useMemo(() => toChronological(sessionHistory), [sessionHistory]);
  const count = sessionHistory.length;

  return (
    <section className="card insights-card">
      <div className="card-header">
        <h2>Insights</h2>
        <span className="pill">
          last {count} session{count === 1 ? "" : "s"}
        </span>
      </div>

      {count === 0 ? (
        <p className="helper-text">
          No completed sessions yet. Finish a session to see your focus trends here.
        </p>
      ) : (
        <>
          <div className="insight-tiles">
            <Tile value={String(aggregates.sessionCount)} label="Sessions" />
            <Tile value={String(Math.round(aggregates.avgFocusScore))} label="Avg focus" />
            <Tile value={`${Math.round(aggregates.avgDeepFocusPct)}%`} label="Deep focus" />
            <Tile value={String(aggregates.totalSnapbacks)} label="Snapbacks" />
          </div>
          <FocusTrendChart summaries={chronological} />
          <p className="insights-caption">
            Avg focus score (0–100) per session · oldest → newest
          </p>
        </>
      )}
    </section>
  );
}
