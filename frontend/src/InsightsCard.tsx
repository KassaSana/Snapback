import { memo, useMemo, useState } from "react";

import { formatScore, type SessionSummary } from "./api";
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

// Roadmap 7.6: "you may inspect and destroy what I collected." The two-step confirm matches
// the Privacy card's danger zone — one click can never delete a session, because there is no
// undo behind this button, and a mis-click costs the user data they cannot get back.
function SessionDeleteList({
  deletingSessionId,
  onDelete,
  summaries,
}: {
  deletingSessionId: string | null;
  onDelete: (sessionId: string) => void | Promise<void>;
  summaries: SessionSummary[];
}) {
  const [confirmingId, setConfirmingId] = useState<string | null>(null);

  return (
    <ul className="rules-list session-list">
      {summaries.map((summary, index) => {
        const sessionId = summary.record.sessionId;
        const label = sessionRowLabel(summary);
        const busy = deletingSessionId === sessionId;
        const confirming = confirmingId === sessionId;

        return (
          <li className="rules-item" key={sessionId || index}>
            <div className="session-row-detail">
              <span className="rules-pattern">{label}</span>
              <p className="rules-note">
                {formatTime(summary.record.startedAt)} ·{" "}
                {Math.round(summary.recap.durationSecs / 60)} min · focus{" "}
                {formatScore(summary.recap.avgFocusScore)}
              </p>
            </div>
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
                disabled={busy}
                aria-label={`Delete session ${label}`}
                onClick={() => setConfirmingId(sessionId)}
              >
                Delete
              </button>
            )}
          </li>
        );
      })}
    </ul>
  );
}

function SessionReflectionList({
  onSaveReflection,
  summaries,
}: {
  onSaveReflection: (
    sessionId: string,
    done: string | null,
    nextStep: string | null,
  ) => boolean | Promise<boolean>;
  summaries: SessionSummary[];
}) {
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

        return (
          <li className="rules-item" key={sessionId || index}>
            <div className="session-row-detail">
              <span className="rules-pattern">{label}</span>
              <p className="rules-note">
                {formatTime(summary.record.startedAt)} ·{" "}
                {Math.round(summary.recap.durationSecs / 60)} min · focus{" "}
                {formatScore(summary.recap.avgFocusScore)}
              </p>
            </div>
            {editingId === sessionId ? (
              <div className="reflection-editor">
                <label className="field-label">What got done?
                  <textarea value={done} maxLength={1000} onChange={(event) => setDone(event.target.value)} />
                </label>
                <label className="field-label">Next step
                  <textarea value={nextStep} maxLength={1000} onChange={(event) => setNextStep(event.target.value)} />
                </label>
                <div className="button-row">
                  <button className="primary-button" disabled={saving} onClick={async () => {
                    setSavingId(sessionId);
                    const saved = await onSaveReflection(
                      sessionId,
                      done.trim() || null,
                      nextStep.trim() || null,
                    );
                    setSavingId(null);
                    if (saved) setEditingId(null);
                  }}>Save reflection</button>
                  <button className="ghost-button" disabled={saving} onClick={() => setEditingId(null)}>Cancel</button>
                </div>
              </div>
            ) : sessionId !== "" ? (
              <button className="secondary-button" onClick={() => {
                setDone(summary.record.reflectionDone ?? "");
                setNextStep(summary.record.reflectionNextStep ?? "");
                setEditingId(sessionId);
              }}>Edit reflection</button>
            ) : null}
          </li>
        );
      })}
    </ul>
  );
}

export const InsightsCard = memo(function InsightsCard({
  deleteError = null,
  deleteStatus = null,
  deletingSessionId = null,
  onDeleteSession,
  onSaveReflection,
  reflectionStatus = null,
  rangeLabel,
  sessionHistory,
}: InsightsCardProps) {
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
        <span className="pill">{rangeLabel}</span>
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
          {onSaveReflection ? (
            <div className="session-reflection-zone">
              <h3>Session reflections</h3>
              <SessionReflectionList
                onSaveReflection={onSaveReflection}
                summaries={sessionHistory}
              />
              {reflectionStatus ? <p className="helper-text">{reflectionStatus}</p> : null}
            </div>
          ) : null}
          {onDeleteSession ? (
            <div className="session-delete-zone">
              <h3>Delete a session</h3>
              <p className="helper-text">
                Removes that session and everything recorded under it — predictions, captured
                window context, and labels. Permanent, and it changes the numbers above.
              </p>
              <SessionDeleteList
                deletingSessionId={deletingSessionId}
                onDelete={onDeleteSession}
                summaries={sessionHistory}
              />
              {deleteStatus ? <p className="helper-text success">{deleteStatus}</p> : null}
              {deleteError ? <p className="helper-text alert">{deleteError}</p> : null}
            </div>
          ) : null}
        </>
      )}
    </section>
  );
});
