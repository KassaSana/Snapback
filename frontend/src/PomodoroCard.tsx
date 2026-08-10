import { memo, useEffect, useState } from "react";

import { formatPomodoroRemaining, type PomodoroConfig, type PomodoroStatus } from "./api";

type PomodoroCardProps = {
  pomodoroStatus: PomodoroStatus;
  pomodoroConfig: PomodoroConfig;
  sessionActive: boolean;
  onStart: () => void;
  onStop: () => void;
  onPause: () => void;
  onResume: () => void;
  onSkip: () => void;
  onRestart: () => void;
  onAcknowledge: () => void;
  onSaveConfig: (config: PomodoroConfig) => void;
};

const PHASE_LABELS: Record<PomodoroStatus["phase"], string> = {
  work: "Work",
  shortBreak: "Short break",
  longBreak: "Long break",
};

export const PomodoroCard = memo(function PomodoroCard({
  pomodoroStatus,
  pomodoroConfig,
  sessionActive,
  onStart,
  onStop,
  onPause,
  onResume,
  onSkip,
  onRestart,
  onAcknowledge,
  onSaveConfig,
}: PomodoroCardProps) {
  const [draft, setDraft] = useState(pomodoroConfig);
  useEffect(() => setDraft(pomodoroConfig), [pomodoroConfig]);
  const { running, paused, awaitingAcknowledgement, phase, completedWorkIntervals, remainingMs } =
    pomodoroStatus;

  // Roadmap 2.13. Three states read as "not counting down" and mean different things, so the
  // countdown says which: paused is the user's choice, awaiting is a finished phase holding
  // for them, and a stopped timer has no time to show at all.
  const remainingText = !running
    ? "--:--"
    : awaitingAcknowledgement
      ? "Done"
      : formatPomodoroRemaining(remainingMs);

  return (
    <section className="card pomodoro-card">
      <div className="card-header">
        <h2>Pomodoro</h2>
        <span className="pill">{completedWorkIntervals} completed</span>
      </div>

      <div className="metrics">
        <div className="metric">
          <p className="metric-label">Phase</p>
          <p className="metric-value">{PHASE_LABELS[phase]}</p>
        </div>
        <div className="metric">
          <p className="metric-label">Remaining</p>
          <p className="metric-value">{remainingText}</p>
        </div>
      </div>

      {paused && running ? (
        <p className="helper-text">Paused. The time left is held until you resume.</p>
      ) : null}
      {awaitingAcknowledgement && running ? (
        <p className="helper-text">
          {PHASE_LABELS[phase]} is ready when you are — it will not start on its own.
        </p>
      ) : null}

      {!sessionActive ? (
        <p className="helper-text">Start a focus session to use the Pomodoro timer.</p>
      ) : !running ? (
        <button className="primary-button" onClick={onStart}>
          Start Pomodoro
        </button>
      ) : (
        <div className="button-row">
          {awaitingAcknowledgement ? (
            <button className="primary-button" onClick={onAcknowledge}>
              Start {PHASE_LABELS[phase].toLowerCase()}
            </button>
          ) : paused ? (
            <button className="primary-button" onClick={onResume}>
              Resume
            </button>
          ) : (
            <button className="secondary-button" onClick={onPause}>
              Pause
            </button>
          )}
          {/* Skip and restart apply to the phase in progress, so they are hidden while one is
              waiting to begin — there is nothing yet to skip or replay. */}
          {awaitingAcknowledgement ? null : (
            <>
              <button className="secondary-button" onClick={onSkip}>
                Skip phase
              </button>
              <button className="secondary-button" onClick={onRestart}>
                Restart phase
              </button>
            </>
          )}
          <button className="secondary-button" onClick={onStop}>
            Stop Pomodoro
          </button>
        </div>
      )}

      <details className="pomodoro-settings">
        <summary>Customize rhythm</summary>
        <div className="form-grid">
          {([
            ["Work minutes", "workMs"],
            ["Short break minutes", "shortBreakMs"],
            ["Long break minutes", "longBreakMs"],
          ] as const).map(([label, key]) => (
            <label className="field-label" key={key}>
              {label}
              <input type="number" min="1" max="180" value={Math.round(draft[key] / 60000)}
                onChange={(event) => setDraft({ ...draft, [key]: Number(event.target.value) * 60000 })} />
            </label>
          ))}
          <label className="field-label">
            Work intervals before a long break
            <input type="number" min="1" max="12" value={draft.intervalsBeforeLongBreak}
              onChange={(event) => setDraft({ ...draft, intervalsBeforeLongBreak: Number(event.target.value) })} />
          </label>
          <label className="toggle-row">
            <input type="checkbox" checked={draft.autoStartNextPhase}
              onChange={(event) => setDraft({ ...draft, autoStartNextPhase: event.target.checked })} />
            Start the next phase automatically
          </label>
          <button className="secondary-button" onClick={() => onSaveConfig(draft)}>
            Save rhythm
          </button>
        </div>
      </details>
    </section>
  );
});
