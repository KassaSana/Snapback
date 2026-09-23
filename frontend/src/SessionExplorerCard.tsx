// Roadmap 2.9 — the session explorer, first version.
//
// Review answered "how was the week" and never "what happened in that session on Tuesday":
// past sessions appeared mostly inside the destructive Session management list. This card
// lists the sessions in the selected range by day, and a selected one opens a detail panel —
// the list-then-detail shape Rize, Timing, ActivityWatch and Session all converge on.
//
// Deliberately left for later: search, filters and paging past the range's 500-session cap,
// and label editing (2.17). "Start this again" hands off to the ordinary start form on Now; it
// never starts recording by itself, because a session is declared (ADR-0005).
import { memo, useEffect, useMemo, useState } from "react";

import {
  api,
  formatScore,
  formatTime,
  type ContextSnapshot,
  type FocusCurvePoint,
  type SessionSummary,
} from "./api";
import { ChartDataTable } from "./ChartDataTable";
import { formatFocusStretch } from "./focusStreak";
import { FOCUS_MODE_LABELS, normalizeFocusMode, type FocusMode } from "./sessionCockpit";

/** How many context rows the detail reads to rank apps. The native cap, so it is one call. */
const CONTEXT_SAMPLE_LIMIT = 500;

type SessionExplorerCardProps = {
  sessionHistory: SessionSummary[];
  rangeLabel: string;
  /** True while a session is running: "Start this again" then has nowhere safe to go. */
  sessionActive: boolean;
  onStartAgain: (goal: string, mode: FocusMode) => void;
  deletingSessionId?: string | null;
  onDeleteSession?: (sessionId: string) => void | Promise<void>;
};

type Detail =
  | { state: "loading" }
  | { state: "error" }
  | {
      state: "ready";
      curve: FocusCurvePoint[];
      apps: { appName: string; count: number }[];
      contextRows: number;
    };

const dayKey = (ms: number | null) => {
  if (ms == null) return "Undated";
  return new Date(ms).toLocaleDateString(undefined, {
    weekday: "long",
    month: "short",
    day: "numeric",
  });
};

const attendedSecs = (summary: SessionSummary) =>
  summary.recap.activeSecs ?? summary.recap.durationSecs;

export const SessionExplorerCard = memo(function SessionExplorerCard({
  sessionHistory,
  rangeLabel,
  sessionActive,
  onStartAgain,
  deletingSessionId,
  onDeleteSession,
}: SessionExplorerCardProps) {
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [detail, setDetail] = useState<Detail>({ state: "loading" });
  const [confirmingDelete, setConfirmingDelete] = useState(false);

  // Newest first, grouped by the local day each session started on.
  const days = useMemo(() => {
    const sorted = [...sessionHistory].sort(
      (a, b) => (b.record.startedAtMs ?? 0) - (a.record.startedAtMs ?? 0),
    );
    const groups: { day: string; sessions: SessionSummary[] }[] = [];
    for (const summary of sorted) {
      const day = dayKey(summary.record.startedAtMs);
      const last = groups[groups.length - 1];
      if (last && last.day === day) last.sessions.push(summary);
      else groups.push({ day, sessions: [summary] });
    }
    return groups;
  }, [sessionHistory]);

  // A selection the range no longer contains (a new range, or a deletion) shows nothing.
  const selected = sessionHistory.find((s) => s.record.sessionId === selectedId) ?? null;

  // Resetting here rather than in the loading effect: the effect then only talks to the
  // backend, and the panel never shows a previous session's detail under a new heading.
  const select = (id: string) => {
    const next = id === selectedId ? null : id;
    setSelectedId(next);
    setConfirmingDelete(false);
    if (next) setDetail({ state: "loading" });
  };

  useEffect(() => {
    if (!selectedId) return;
    let current = true;
    Promise.all([
      api.getSessionFocusCurve(selectedId),
      api.getContextTimeline(selectedId, CONTEXT_SAMPLE_LIMIT),
    ])
      .then(([curve, context]: [FocusCurvePoint[], ContextSnapshot[]]) => {
        if (!current) return;
        const counts = new Map<string, number>();
        for (const row of context) {
          if (row.appName) counts.set(row.appName, (counts.get(row.appName) ?? 0) + 1);
        }
        const apps = [...counts.entries()]
          .map(([appName, count]) => ({ appName, count }))
          .sort((a, b) => b.count - a.count || a.appName.localeCompare(b.appName))
          .slice(0, 5);
        setDetail({ state: "ready", curve, apps, contextRows: context.length });
      })
      .catch(() => {
        if (current) setDetail({ state: "error" });
      });
    return () => {
      current = false;
    };
  }, [selectedId]);

  return (
    <section className="card session-explorer-card">
      <div className="card-header">
        <h2>Sessions</h2>
        <span className="pill">{rangeLabel}</span>
      </div>
      {days.length === 0 ? (
        <p className="helper-text">No sessions in this range.</p>
      ) : (
        <div className="session-explorer">
          <div className="session-explorer-list" aria-label="Sessions by day">
            {days.map((group) => (
              <div key={group.day} className="session-explorer-day">
                <h3>{group.day}</h3>
                <ul>
                  {group.sessions.map((summary) => {
                    const id = summary.record.sessionId;
                    const mode = normalizeFocusMode(summary.record.focusMode);
                    return (
                      <li key={id}>
                        <button
                          type="button"
                          className="session-explorer-row"
                          aria-pressed={id === selectedId}
                          onClick={() => select(id)}
                        >
                          <span className="session-explorer-goal">
                            {summary.record.goal || "Untitled session"}
                          </span>
                          <span className="session-explorer-meta">
                            {formatTime(summary.record.startedAtMs)} · {FOCUS_MODE_LABELS[mode]} ·{" "}
                            {formatFocusStretch(attendedSecs(summary))} · focus{" "}
                            {Math.round(summary.recap.avgFocusScore)}
                          </span>
                        </button>
                      </li>
                    );
                  })}
                </ul>
              </div>
            ))}
          </div>

          {selected ? (
            <SessionDetail
              summary={selected}
              detail={detail}
              sessionActive={sessionActive}
              onStartAgain={onStartAgain}
              confirmingDelete={confirmingDelete}
              setConfirmingDelete={setConfirmingDelete}
              deleting={deletingSessionId === selected.record.sessionId}
              onDeleteSession={onDeleteSession}
            />
          ) : (
            <p className="helper-text session-explorer-empty">
              Choose a session to see its focus over time, where you worked, and your reflection.
            </p>
          )}
        </div>
      )}
    </section>
  );
});

function SessionDetail({
  summary,
  detail,
  sessionActive,
  onStartAgain,
  confirmingDelete,
  setConfirmingDelete,
  deleting,
  onDeleteSession,
}: {
  summary: SessionSummary;
  detail: Detail;
  sessionActive: boolean;
  onStartAgain: (goal: string, mode: FocusMode) => void;
  confirmingDelete: boolean;
  setConfirmingDelete: (value: boolean) => void;
  deleting: boolean;
  onDeleteSession?: (sessionId: string) => void | Promise<void>;
}) {
  const { record, recap } = summary;
  const mode = normalizeFocusMode(record.focusMode);
  const goal = record.goal || "Untitled session";

  return (
    <div className="session-detail" role="region" aria-label={`Session: ${goal}`}>
      <h3>{goal}</h3>
      <p className="helper-text">
        {FOCUS_MODE_LABELS[mode]} · {formatTime(record.startedAtMs)}
        {record.endedAtMs != null ? ` – ${formatTime(record.endedAtMs)}` : " · still running"}
      </p>

      <dl className="session-detail-stats">
        <div>
          <dt>Attended</dt>
          <dd>{formatFocusStretch(attendedSecs(summary))}</dd>
        </div>
        <div>
          <dt>Avg focus</dt>
          <dd>{formatScore(recap.avgFocusScore)}</dd>
        </div>
        <div>
          <dt>Deep work</dt>
          <dd>{Math.round(recap.deepFocusPct)}%</dd>
        </div>
        <div>
          <dt>Snapbacks</dt>
          <dd>{recap.snapbackCount}</dd>
        </div>
      </dl>

      {record.reflectionDone || record.reflectionNextStep ? (
        <div className="session-detail-reflection">
          {record.reflectionDone ? (
            <p>
              <strong>Done:</strong> {record.reflectionDone}
            </p>
          ) : null}
          {record.reflectionNextStep ? (
            <p>
              <strong>Next:</strong> {record.reflectionNextStep}
            </p>
          ) : null}
        </div>
      ) : null}

      {detail.state === "loading" ? (
        <p className="helper-text" role="status">
          Loading this session…
        </p>
      ) : detail.state === "error" ? (
        <p className="helper-text alert" role="alert">
          Could not load this session's detail.
        </p>
      ) : (
        <>
          <h4>Focus over the session</h4>
          {detail.curve.length === 0 ? (
            <p className="helper-text">No predictions were recorded in this session.</p>
          ) : (
            <>
              <FocusCurve curve={detail.curve} />
              <ChartDataTable
                caption={`Focus over the session: ${goal}`}
                nameHeader="From"
                rows={detail.curve.map((point) => ({
                  key: String(point.startMs),
                  name: formatTime(point.startMs),
                  detail: `focus ${Math.round(point.avgFocusScore)} of 100 · ${point.sampleCount} ${
                    point.sampleCount === 1 ? "sample" : "samples"
                  }`,
                }))}
              />
            </>
          )}

          <h4>Where you worked</h4>
          {detail.apps.length === 0 ? (
            <p className="helper-text">No window context was recorded.</p>
          ) : (
            <>
              <ul className="session-detail-apps">
                {detail.apps.map((app) => (
                  <li key={app.appName}>
                    <span>{app.appName}</span>
                    <span className="meta-sub">
                      {app.count} {app.count === 1 ? "sample" : "samples"}
                    </span>
                  </li>
                ))}
              </ul>
              <p className="insights-caption">
                Context samples are periodic observations of the focused window, not app switches
                {detail.contextRows >= CONTEXT_SAMPLE_LIMIT
                  ? `; ranked from the first ${CONTEXT_SAMPLE_LIMIT} of them.`
                  : "."}
              </p>
            </>
          )}
        </>
      )}

      <div className="button-row">
        <button
          type="button"
          className="secondary-button"
          disabled={sessionActive}
          onClick={() => onStartAgain(record.goal, mode)}
        >
          Start this again
        </button>
        {onDeleteSession ? (
          confirmingDelete ? (
            <>
              <button
                type="button"
                className="danger-button"
                disabled={deleting}
                onClick={() => {
                  setConfirmingDelete(false);
                  void onDeleteSession(record.sessionId);
                }}
              >
                {deleting ? "Deleting…" : "Confirm delete"}
              </button>
              <button
                type="button"
                className="link-button"
                onClick={() => setConfirmingDelete(false)}
              >
                Cancel
              </button>
            </>
          ) : (
            <button
              type="button"
              className="link-button"
              disabled={deleting}
              onClick={() => setConfirmingDelete(true)}
            >
              Delete this session…
            </button>
          )
        ) : null}
      </div>
      {sessionActive ? (
        <p className="helper-text">
          A session is running. Stop it on Now before starting this one again.
        </p>
      ) : null}
    </div>
  );
}

/** Bars on a fixed 0–100 axis, like every other focus chart on Review (10.8). */
function FocusCurve({ curve }: { curve: FocusCurvePoint[] }) {
  const w = 480;
  const h = 120;
  const pad = 22;
  const baseline = h - 14;
  const plotH = baseline - 8;
  const slot = (w - pad * 2) / curve.length;
  const barW = Math.max(1, Math.min(14, slot * 0.8));
  return (
    <svg
      className="insights-chart"
      viewBox={`0 0 ${w} ${h}`}
      role="img"
      aria-label="Focus over the session, 0 to 100, earliest to latest; data table below"
    >
      <line x1={pad} y1={baseline} x2={w - pad} y2={baseline} className="chart-baseline" />
      <line
        x1={pad}
        y1={baseline - plotH / 2}
        x2={w - pad}
        y2={baseline - plotH / 2}
        className="chart-midline"
      />
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
      {curve.map((point, index) => {
        const score = Math.min(100, Math.max(0, point.avgFocusScore));
        const barH = Math.max(1, (score / 100) * plotH);
        return (
          <rect
            key={point.startMs}
            x={pad + index * slot + (slot - barW) / 2}
            y={baseline - barH}
            width={barW}
            height={barH}
            className="chart-bar"
          >
            <title>{`${formatTime(point.startMs)} · focus ${Math.round(score)}`}</title>
          </rect>
        );
      })}
    </svg>
  );
}
