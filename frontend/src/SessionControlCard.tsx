import { memo, useEffect, useMemo, useState } from "react";

import { formatTime, type SessionRecord } from "./api";
import {
  FOCUS_MODES,
  addSessionPreset,
  canStartSession,
  canStopSession,
  filterGoalSuggestions,
  formatElapsed,
  moveSessionPreset,
  readSessionPresets,
  removeSessionPreset,
  validateSessionGoal,
  writeSessionPresets,
  type FocusMode,
  type RecentGoal,
  type SessionPreset,
} from "./sessionCockpit";


type SessionControlCardProps = {
  focusMode: FocusMode;
  handleFocusModeChange: (mode: FocusMode) => void;
  handleStartSession: () => void;
  handleStopSession: () => void;
  /** Roadmap 2.11's guarded switch: stops the running session, then starts the typed one. */
  handleSwitchSession: () => void;
  sessionGoal: string;
  sessionId: string | null;
  sessionRecord: SessionRecord | null;
  sessionStatusLabel: string;
  setSessionGoal: (value: string) => void;
  /** True while a start/stop request is in flight, so the controls can go quiet. */
  sessionPending: boolean;
  /** Distinct goals from recent history, newest first. */
  recentGoals: RecentGoal[];
  /**
   * Set when the user has been working steadily with no session running (Roadmap 2.7 /
   * ADR-0005). Shown here rather than as a toast because this is where the answer lives:
   * the Start button is one click away.
   */
  untrackedNote: string | null;
  dismissUntrackedNote: () => void;
};

export const SessionControlCard = memo(function SessionControlCard({
  focusMode,
  handleFocusModeChange,
  handleStartSession,
  handleStopSession,
  handleSwitchSession,
  sessionGoal,
  sessionId,
  sessionRecord,
  sessionStatusLabel,
  setSessionGoal,
  sessionPending,
  recentGoals,
  untrackedNote,
  dismissUntrackedNote,
}: SessionControlCardProps) {
  // Roadmap 2.11. `pristine` keeps the validation message off an untouched form. It clears on
  // the first keystroke and never comes back for the life of the card, which is what makes the
  // message read as an answer to something the user did.
  const [pristine, setPristine] = useState(true);
  const [presets, setPresets] = useState<SessionPreset[]>(() => readSessionPresets());
  const [switching, setSwitching] = useState(false);
  const [showSuggestions, setShowSuggestions] = useState(false);
  const [highlightedIndex, setHighlightedIndex] = useState(-1);

  const suggestions = useMemo(
    () => filterGoalSuggestions(recentGoals, presets, sessionGoal),
    [recentGoals, presets, sessionGoal],
  );

  const sessionActive = sessionRecord?.status === "ACTIVE";
  const validation = validateSessionGoal(sessionGoal, pristine);
  // While a session runs, the form is only reachable through the guarded switch, so the
  // gate is the same validation with the live session no longer disqualifying it.
  const startable = canStartSession(sessionGoal, sessionPending, sessionActive);
  const switchable =
    switching && !sessionPending && validateSessionGoal(sessionGoal).valid && sessionActive;
  const stoppable = canStopSession(sessionRecord, sessionPending);

  // The origin is the backend's `started_at`; this only supplies the tick. It runs solely for a
  // live session so an idle cockpit is not re-rendering once a second forever.
  const [nowMs, setNowMs] = useState(() => Date.now());
  useEffect(() => {
    if (!sessionActive) return;
    setNowMs(Date.now());
    const id = setInterval(() => setNowMs(Date.now()), 1000);
    return () => clearInterval(id);
  }, [sessionActive]);

  const updatePresets = (next: SessionPreset[]) => {
    setPresets(next);
    writeSessionPresets(next);
  };

  // Applying a preset or a recent goal fills the form and stops. ADR-0005 keeps declaration
  // explicit, so the user still presses Start.
  const applyGoal = (goal: string, mode: FocusMode) => {
    setPristine(false);
    setSessionGoal(goal);
    handleFocusModeChange(mode);
    setShowSuggestions(false);
    setHighlightedIndex(-1);
  };

  const handleGoalKeyDown = (event: React.KeyboardEvent<HTMLInputElement>) => {
    if (suggestions.length === 0) return;

    if (event.key === "ArrowDown") {
      event.preventDefault();
      setShowSuggestions(true);
      setHighlightedIndex((prev) => (prev + 1) % suggestions.length);
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      setShowSuggestions(true);
      setHighlightedIndex((prev) => (prev <= 0 ? suggestions.length - 1 : prev - 1));
    } else if (event.key === "Enter" && showSuggestions && highlightedIndex >= 0) {
      const selected = suggestions[highlightedIndex];
      if (selected) {
        event.preventDefault();
        applyGoal(selected.goal, selected.focusMode);
      }
    } else if (event.key === "Escape") {
      setShowSuggestions(false);
      setHighlightedIndex(-1);
    }
  };

  const onSubmit = (event: React.FormEvent) => {
    // Enter in the goal field submits, which is the whole point; without this the key does
    // nothing and the user learns the form is inert.
    event.preventDefault();
    setPristine(false);
    setShowSuggestions(false);
    setHighlightedIndex(-1);
    if (switching) {
      if (switchable) handleSwitchSession();
      return;
    }
    if (startable) handleStartSession();
  };


  return (
    <section className="card session-card">
      <div className="card-header">
        <h2>Session Control</h2>
        <span className="session-status">{sessionStatusLabel}</span>
      </div>
      {untrackedNote && (
        <div className="notice notice-untracked" role="status">
          <p>{untrackedNote}</p>
          <button
            type="button"
            className="link-button"
            onClick={dismissUntrackedNote}
            aria-label="Dismiss the untracked work notice"
          >
            Dismiss
          </button>
        </div>
      )}

      {sessionActive && (
        <div className="session-live">
          <div className="metrics">
            <div className="metric">
              <p className="metric-label">Working on</p>
              <p className="metric-value">{sessionRecord?.goal || "--"}</p>
            </div>
            <div className="metric">
              <p className="metric-label">Elapsed</p>
              <p className="metric-value" aria-label="Elapsed session time">
                {formatElapsed(sessionRecord?.startedAtMs, nowMs)}
              </p>
            </div>
            <div className="metric">
              <p className="metric-label">Mode</p>
              <p className="metric-value">{sessionRecord?.focusMode || focusMode}</p>
            </div>
          </div>
          <div className="button-row">
            <button
              type="button"
              className="secondary-button"
              onClick={handleStopSession}
              disabled={!stoppable}
            >
              {sessionPending ? "Working…" : "Stop session"}
            </button>
            <button
              type="button"
              className="link-button"
              onClick={() => setSwitching((value) => !value)}
              aria-expanded={switching}
              disabled={sessionPending}
            >
              {switching ? "Keep this session" : "Start a different session"}
            </button>
          </div>
          {switching && (
            <p className="helper-text" role="status">
              Starting a different session stops this one first and saves its recap. Nothing is
              lost, but the elapsed time above stops here.
            </p>
          )}
        </div>
      )}

      {(!sessionActive || switching) && (
        <form onSubmit={onSubmit}>
          <label className="field">
            <span>Focus goal</span>
            <div className="goal-input-wrapper">
              <input
                type="text"
                placeholder="Ship the snapback overlay"
                value={sessionGoal}
                onChange={(event) => {
                  setPristine(false);
                  setSessionGoal(event.target.value);
                  setShowSuggestions(true);
                  setHighlightedIndex(-1);
                }}
                onFocus={() => {
                  if (suggestions.length > 0) setShowSuggestions(true);
                }}
                onBlur={(event) => {
                  if (!event.currentTarget.parentElement?.contains(event.relatedTarget)) {
                    setShowSuggestions(false);
                    setHighlightedIndex(-1);
                  }
                }}
                onKeyDown={handleGoalKeyDown}
                aria-invalid={Boolean(validation.message)}
                aria-describedby={validation.message ? "session-goal-error" : undefined}
                aria-autocomplete="list"
                aria-controls="goal-suggestions-list"
                disabled={sessionPending}
              />
              {showSuggestions && suggestions.length > 0 && (
                <ul
                  id="goal-suggestions-list"
                  className="goal-suggestions-dropdown"
                  role="listbox"
                  aria-label="Suggested goals"
                >
                  {suggestions.map((suggestion, index) => (
                    <li
                      key={`${suggestion.source}-${suggestion.goal}`}
                      id={`goal-suggestion-${index}`}
                      role="option"
                      aria-selected={index === highlightedIndex}
                      className={`goal-suggestion-item ${index === highlightedIndex ? "is-highlighted" : ""}`}
                      onMouseDown={(e) => {
                        e.preventDefault();
                        applyGoal(suggestion.goal, suggestion.focusMode);
                      }}
                    >
                      <span className="suggestion-goal-text">{suggestion.goal}</span>
                      <span className="suggestion-meta-badge">
                        {suggestion.source === "pinned" ? "Pinned" : "Recent"} · {suggestion.focusMode}
                      </span>
                    </li>
                  ))}
                </ul>
              )}
            </div>
          </label>

          {validation.message && (
            <p className="helper-text helper-error" id="session-goal-error" role="alert">
              {validation.message}
            </p>
          )}

          <label className="field">
            <span>Focus mode</span>
            <select
              value={focusMode}
              onChange={(event) =>
                void handleFocusModeChange(event.target.value as FocusMode)
              }
              disabled={sessionPending}
            >
              {FOCUS_MODES.map((mode) => (
                <option key={mode} value={mode}>
                  {mode}
                </option>
              ))}
            </select>
          </label>

          <div className="button-row">
            <button
              type="submit"
              className="primary-button"
              disabled={switching ? !switchable : !startable}
            >
              {sessionPending
                ? "Working…"
                : switching
                  ? "Stop and start this one"
                  : "Start session"}
            </button>
            {!sessionActive && recentGoals.length > 0 && (
              <button
                type="button"
                className="secondary-button"
                onClick={() => applyGoal(recentGoals[0].goal, recentGoals[0].focusMode)}
                disabled={sessionPending}
                title={recentGoals[0].goal}
              >
                Repeat last
              </button>
            )}
            {validateSessionGoal(sessionGoal).valid && (
              <button
                type="button"
                className="link-button"
                onClick={() => updatePresets(addSessionPreset(presets, sessionGoal, focusMode))}
                disabled={sessionPending}
              >
                Pin this goal
              </button>
            )}
          </div>

          {recentGoals.length > 0 && (
            <div className="recent-goals">
              <p className="meta-label">Recent goals</p>
              <ul className="chip-list">
                {recentGoals.map((entry) => (
                  <li key={entry.goal}>
                    <button
                      type="button"
                      className="chip"
                      onClick={() => applyGoal(entry.goal, entry.focusMode)}
                      disabled={sessionPending}
                    >
                      {entry.goal}
                    </button>
                  </li>
                ))}
              </ul>
            </div>
          )}

          {presets.length > 0 && (
            <div className="session-presets">
              <p className="meta-label">Pinned</p>
              <ul className="preset-list">
                {presets.map((preset, index) => (
                  <li key={preset.id} className="preset-row">
                    <button
                      type="button"
                      className="chip"
                      onClick={() => applyGoal(preset.goal, preset.focusMode)}
                      disabled={sessionPending}
                    >
                      {preset.goal} · {preset.focusMode}
                    </button>
                    <button
                      type="button"
                      className="icon-button"
                      aria-label={`Move ${preset.goal} up`}
                      disabled={index === 0 || sessionPending}
                      onClick={() => updatePresets(moveSessionPreset(presets, preset.id, "up"))}
                    >
                      ↑
                    </button>
                    <button
                      type="button"
                      className="icon-button"
                      aria-label={`Move ${preset.goal} down`}
                      disabled={index === presets.length - 1 || sessionPending}
                      onClick={() => updatePresets(moveSessionPreset(presets, preset.id, "down"))}
                    >
                      ↓
                    </button>
                    <button
                      type="button"
                      className="icon-button"
                      aria-label={`Unpin ${preset.goal}`}
                      disabled={sessionPending}
                      onClick={() => updatePresets(removeSessionPreset(presets, preset.id))}
                    >
                      ✕
                    </button>
                  </li>
                ))}
              </ul>
              <p className="helper-text">
                Pinned goals fill the form. They never start a session on their own.
              </p>
            </div>
          )}
        </form>
      )}

      {/*
        Roadmap 2.11. The session UUID is support-desk detail, not the headline it used to be.
        It stays reachable and copyable — a support instruction that says "send us your session
        id" must still work — but it no longer occupies the space where elapsed time belongs.
      */}
      <details className="technical-details">
        <summary>Technical details</summary>
        <div className="meta">
          <div>
            <p className="meta-label">Session ID</p>
            <p className="meta-value">
              <code>{sessionId || "--"}</code>
            </p>
          </div>
          <div>
            <p className="meta-label">Started</p>
            <p className="meta-value">{formatTime(sessionRecord?.startedAtMs ?? null)}</p>
          </div>
          <div>
            <p className="meta-label">Ended</p>
            <p className="meta-value">{formatTime(sessionRecord?.endedAtMs ?? null)}</p>
          </div>
        </div>
      </details>
    </section>
  );
});
