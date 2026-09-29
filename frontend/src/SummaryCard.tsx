import { memo } from "react";
import { FOCUS_STRETCH_LABEL, formatFocusStretch } from "./focusStreak";

import type { FocusSummary, SummaryReport } from "./api";

import { Tile } from "./InsightsCard";

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
  // Attended can be shown even with no prediction history.
  const attendedSeconds = report.attendedSeconds;
  const showAttended = attendedSeconds > 0 || report.plannedMins > 0 || hasHistory;
  // Attendance for today/7d is measured over the calendar day/week, unlike the rolling window
  // the pill names, so say which period it covers.
  const attendedPeriod =
    report.window === "day"
      ? "since midnight"
      : report.window === "week" || report.window === "7d"
        ? "this calendar week"
        : null;

  // "Session time" is start-to-end of completed sessions, not focused time. Say when the
  // newest-N session cap applied.
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
                value={formatFocusStretch(attendedSeconds)}
                label="Attended"
                detail={[
                  report.plannedMins > 0
                    ? `of ${formatMinutes(report.plannedMins)} planned (${Math.round((attendedSeconds / (report.plannedMins * 60)) * 100)}%)`
                    : "measured, not scored",
                  attendedPeriod,
                ]
                  .filter(Boolean)
                  .join(" · ")}
              />
            ) : null}
            <Tile
              value={formatFocusStretch(report.focusSeconds)}
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
