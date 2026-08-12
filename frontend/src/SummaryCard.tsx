import { memo } from "react";
import { FOCUS_STRETCH_LABEL, formatFocusStretch } from "./focusStreak";

import type { SummaryReport } from "./api";

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
  exportStatus: string | null;
  onExport: () => void;
  rangeLabel: string;
  report: SummaryReport;
};

export const SummaryCard = memo(function SummaryCard({
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

  return (
    <section className="card insights-card">
      <div className="card-header">
        <h2>Summary</h2>
        <span className="pill">{rangeLabel}</span>
      </div>
      {hasHistory || showAttended ? (
        <>
          <div className="insight-tiles">
            {showAttended ? (
              <div className="insight-tile">
                <p className="insight-tile-value">{formatMinutes(attendedMins)}</p>
                <p className="insight-tile-label">Attended</p>
                {report.plannedMins > 0 ? (
                  <p className="meta-sub">
                    of {formatMinutes(report.plannedMins)} planned (
                    {Math.round((attendedMins / report.plannedMins) * 100)}%)
                  </p>
                ) : (
                  <p className="meta-sub">measured, not scored</p>
                )}
              </div>
            ) : null}
            <div className="insight-tile"><p className="insight-tile-value">{formatDuration(report.focusSeconds)}</p><p className="insight-tile-label">Focus time</p></div>
            <div className="insight-tile"><p className="insight-tile-value">{report.sessionCount}</p><p className="insight-tile-label">Sessions</p></div>
            <div className="insight-tile"><p className="insight-tile-value">{Math.round(report.avgFocusScore)}</p><p className="insight-tile-label">Avg focus</p></div>
            <div className="insight-tile"><p className="insight-tile-value">{formatFocusStretch(report.longestFocusSecs)}</p><p className="insight-tile-label">{FOCUS_STRETCH_LABEL}</p></div>
          </div>
          <p className="helper-text">
            {report.topContextApp ? `Most common context: ${report.topContextApp}.` : "No context leader yet."}
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
      <div className="button-row">
        <button
          className="secondary-button"
          disabled={!hasHistory}
          onClick={onExport}
        >
          Export summary
        </button>
      </div>
      {exportStatus ? <p className="helper-text">{exportStatus}</p> : null}
    </section>
  );
});
