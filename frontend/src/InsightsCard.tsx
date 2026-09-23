import { memo, useMemo, useState } from "react";

import { formatScore, type SessionSummary } from "./api";
import { ChartDataTable } from "./ChartDataTable";
import {
  computeInsightsAggregates,
  focusBarHeightPct,
  sessionRowLabel,
  toChronological,
} from "./insightsMetrics";
import { formatTime } from "./utils";

type InsightsCardProps = {
  deleteError?: string | null;
  deleteStatus?: string | null;
  deletingSessionId?: string | null;
  onDeleteSession?: (sessionId: string) => void | Promise<void>;
  onSaveReflection?: (
    sessionId: string,
    done: string | null,
    nextStep: string | null,
  ) => boolean | Promise<boolean>;
  reflectionStatus?: string | null;
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

// Roadmap 7.6: "you may inspect and destroy what I collected." The two-step confirm matches
// the Privacy card's danger zone — one click can never delete a session, because there is no
// undo behind this button, and a mis-click costs the user data they cannot get back.
function SessionManagementList({
  onSaveReflection,
  onDelete,
  deletingSessionId,
  summaries,
}: {
  onDelete?: (sessionId: string) => void | Promise<void>;
  deletingSessionId: string | null;
  onSaveReflection?: (
    sessionId: string,
    done: string | null,
    nextStep: string | null,
  ) => boolean | Promise<boolean>;
  summaries: SessionSummary[];
}) {
  const [confirmingId, setConfirmingId] = useState<string | null>(null);
  const [editingId, setEditingId] = useState<string | null>(null);
  const [done, setDone] = useState("");
  const [nextStep, setNextStep] = useState("");
  const [savingId, setSavingId] = useState<string | null>(null);

  return (
    <ul className="rules-list session-list">
      {summaries.map((summary, index) => {
        const sessionId = summary.record.sessionId;
        const label = sessionRowLabel(summary);
        const saving = savingId === sessionId;
        const busy = saving || deletingSessionId === sessionId;
        const confirming = confirmingId === sessionId;

        return (
          <li className="rules-item" key={sessionId || index}>
            <div className="session-row-detail">
              <span className="rules-pattern">{label}</span>
              <p className="rules-note">
                {formatTime(summary.record.startedAtMs)} ·{" "}
                {Math.round(summary.recap.durationSecs / 60)} min · focus{" "}
                {formatScore(summary.recap.avgFocusScore)}
              </p>
            </div>
            {onDelete ? (
              <>
                {sessionId === "" ? null : confirming ? (
                  <div className="button-row">
                    <button
                      className="danger-button rules-delete"
                      disabled={busy}
                      aria-label={`Confirm delete session ${label}`}
                      onClick={() => {
                        setConfirmingId(null);
                        void onDelete(sessionId);
                      }}
                    >
                      Confirm delete
                    </button>
                    <button
                      className="secondary-button rules-delete"
                      disabled={busy}
                      onClick={() => setConfirmingId(null)}
                    >
                      Cancel
                    </button>
                  </div>
                ) : (
                  <button
                    className="secondary-button rules-delete"
                    disabled={busy || editingId !== null}
                    aria-label={`Delete session ${label}`}
                    onClick={() => setConfirmingId(sessionId)}
                  >
                    Delete
                  </button>
                )}
              </>
            ) : null}
            {onSaveReflection && editingId === sessionId ? (
              <div className="reflection-editor">
                <label className="field-label">
                  What got done?
                  <textarea
                    value={done}
                    maxLength={1000}
                    onChange={(event) => setDone(event.target.value)}
                  />
                </label>
                <label className="field-label">
                  Next step
                  <textarea
                    value={nextStep}
                    maxLength={1000}
                    onChange={(event) => setNextStep(event.target.value)}
                  />
                </label>
                <div className="button-row">
                  <button
                    className="primary-button"
                    disabled={busy}
                    onClick={async () => {
                      setSavingId(sessionId);
                      const saved = await onSaveReflection(
                        sessionId,
                        done.trim() || null,
                        nextStep.trim() || null,
                      );
                      setSavingId(null);
                      if (saved) setEditingId(null);
                    }}
                  >
                    Save reflection
                  </button>
                  <button
                    className="ghost-button"
                    disabled={busy}
                    onClick={() => setEditingId(null)}
                  >
                    Cancel
                  </button>
                </div>
              </div>
            ) : onSaveReflection && sessionId !== "" ? (
              <button
                className="secondary-button"
                disabled={busy || savingId !== null}
                onClick={() => {
                  setConfirmingId(null);
                  setDone(summary.record.reflectionDone ?? "");
                  setNextStep(summary.record.reflectionNextStep ?? "");
                  setEditingId(sessionId);
                }}
              >
                Edit reflection
              </button>
            ) : null}
          </li>
        );
      })}
    </ul>
  );
}

export const InsightsCard = memo(function InsightsCard({
  rangeLabel,
  sessionHistory,
  truncationNote = null,
}: Pick<InsightsCardProps, "rangeLabel" | "sessionHistory"> & {
  /**
   * Roadmap 10.11. Set when the session list behind this chart hit the backend's cap, so
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
        <span className="pill">{rangeLabel}</span>
      </div>
      {sessionHistory.length === 0 ? (
        <p className="helper-text">
          No completed sessions yet. Finish a session to see your focus trends here.
        </p>
      ) : (
        <>
          <FocusTrendChart summaries={chronological} />
          <p className="insights-caption">
            Avg focus score (0–100) per session · oldest → newest
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

export const SessionManagementCard = memo(function SessionManagementCard({
  deleteError = null,
  deleteStatus = null,
  deletingSessionId = null,
  onDeleteSession,
  onSaveReflection,
  reflectionStatus = null,
  sessionHistory,
}: Omit<InsightsCardProps, "rangeLabel">) {
  return (
    <section className="card session-management-card">
      <details>
        <summary>Session management</summary>
        <p className="helper-text">
          Edit reflections or delete a session. Deletion permanently removes its predictions,
          captured window context, and labels, and changes the numbers above.
        </p>
        {sessionHistory.length ? (
          <SessionManagementList
            summaries={sessionHistory}
            onSaveReflection={onSaveReflection}
            onDelete={onDeleteSession}
            deletingSessionId={deletingSessionId}
          />
        ) : (
          <p className="helper-text">No sessions to manage in this range.</p>
        )}
        {reflectionStatus ? (
          <p className="helper-text" role="status">
            {reflectionStatus}
          </p>
        ) : null}
        {deleteStatus ? (
          <p className="helper-text success" role="status">
            {deleteStatus}
          </p>
        ) : null}
        {deleteError ? (
          <p className="helper-text alert" role="alert">
            {deleteError}
          </p>
        ) : null}
      </details>
    </section>
  );
});
