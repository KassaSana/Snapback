import { memo } from "react";
import { FOCUS_STRETCH_LABEL, formatFocusStretch } from "./focusStreak";

import type { FocusSummary, SummaryReport } from "./api";

import { Tile } from "./InsightsCard";

const formatDuration = (seconds: number) => {
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m`;
  return `${Math.floor(minutes / 60)}h ${minutes % 60}m`;
};

const formatMinutes = (mins: number): string => {
  const hours = Math.floor(mins / 60);
  const rest = mins % 60;
  return hours > 0 ? `${hours}h ${rest}m` : `${rest}m`;
};

type SummaryCardProps = {
  focusSummary?: FocusSummary;
  exportStatus: string | null;
  onExport: () => void;
  rangeLabel: string;
  report: SummaryReport;
};

export const SummaryCard = memo(function SummaryCard({
  focusSummary,
  exportStatus,
  onExport,
  rangeLabel,
  report,
}: SummaryCardProps) {
  const hasHistory = report.sampleCount > 0 || report.completedSessionCount > 0;
  // Roadmap 2.19. Attended can be worth showing even when prediction history is empty —
  // spans are the plan's actuals, and a quiet morning with a target still has a comparison.
  const attendedMins = Math.floor(report.attendedSeconds / 60);
  const showAttended = attendedMins > 0 || report.plannedMins > 0 || hasHistory;
  // Attendance is compared against a daily or weekly plan, so for those two presets the
  // backend measures it over the local calendar day / week while every other tile on this
  // card uses the rolling window the range pill names. Say which period this figure covers
  // instead of letting "Last 24h" stand over a since-midnight number.
  const attendedPeriod =
    report.window === "day"
      ? "since midnight"
      : report.window === "week" || report.window === "7d"
        ? "this calendar week"
        : null;

  // "Session time" is the wall clock of completed sessions, start to end -- it includes idle
  // and distracted stretches and is not the model's focused time, which is why it is no longer
  // labelled "Focus time". The session aggregates also read only the newest N sessions; when
  // that cap bit, say so rather than let "All time" quietly mean "the latest 500".
  const sessionDetail = (base?: string) => {
    const cap = report.sessionsTruncated ? `latest ${report.sessionLimit} sessions only` : null;
    return [base, cap].filter(Boolean).join(" · ") || undefined;
  };

  return (
    <section className="card insights-card review-overview">
      <div className="card-header">
        <h2>Summary</h2>
        <div className="button-row">
          <span className="pill">{rangeLabel}</span>
          <button className="secondary-button" disabled={!hasHistory} onClick={onExport}>
            Export summary
          </button>
        </div>
      </div>
      {hasHistory || showAttended ? (
        <>
          <div className="insight-tiles">
            {showAttended ? (
              <Tile
                value={formatMinutes(attendedMins)}
                label="Attended"
                detail={[
                  report.plannedMins > 0
                    ? `of ${formatMinutes(report.plannedMins)} planned (${Math.round((attendedMins / report.plannedMins) * 100)}%)`
                    : "measured, not scored",
                  attendedPeriod,
                ]
                  .filter(Boolean)
                  .join(" · ")}
              />
            ) : null}
            <Tile
              value={formatDuration(report.focusSeconds)}
              label="Session time"
              detail={sessionDetail("completed, start to end")}
            />
            <Tile value={String(report.sessionCount)} label="Sessions" detail={sessionDetail()} />
            <Tile value={String(Math.round(report.avgFocusScore))} label="Avg focus" />
            <Tile value={formatFocusStretch(report.longestFocusSecs)} label={FOCUS_STRETCH_LABEL} />
          </div>
          <p className="helper-text">
            {focusSummary && focusSummary.sampleCount > 0 ? (
              <span>Peak focus {Math.round(focusSummary.peakFocusScore)}</span>
            ) : null}{" "}
            {report.topContextApp
              ? `Most common context: ${report.topContextApp}.`
              : "No context leader yet."}
            {hasHistory
              ? ` ${Math.round(report.distractedFraction * 100)}% of predictions were distracted.`
              : ""}
          </p>
        </>
      ) : (
        <p className="helper-text">
          No summary data for this range yet. Complete a session to build your report.
        </p>
      )}
      {exportStatus ? <p className="helper-text">{exportStatus}</p> : null}
    </section>
  );
});
