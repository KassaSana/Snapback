import { memo, useEffect, useMemo, useState } from "react";

import type { SessionRecord } from "./api";
import {
  FOCUS_MODES,
  FOCUS_MODE_HINT,
  FOCUS_MODE_LABELS,
  addSessionPreset,
  canStartSession,
  canStopSession,
  filterGoalSuggestions,
  formatElapsed,
  moveSessionPreset,
  normalizeFocusMode,
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
  /** Draft only: the mode the next session starts with. Start commits it; nothing else does. */
  setDraftFocusMode: (mode: FocusMode) => void;
  handleStartSession: () => void;
  handleStopSession: () => void;
  /** Roadmap 2.11's guarded switch: stops the running session, then starts the typed one. */
  handleSwitchSession: () => void | Promise<boolean>;
  /** "Keep this session": puts the draft back to the running session's goal and mode. */
  cancelSwitch: () => void;
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
  setDraftFocusMode,
  handleStartSession,
  handleStopSession,
  handleSwitchSession,
  cancelSwitch,
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
  const suggestionsOpen = showSuggestions && suggestions.length > 0;

  const sessionActive = sessionRecord?.status === "ACTIVE";
  // The switch interaction ends with the session it was about: a successful switch changes
  // the id, a Stop (or a replacement start that failed after the stop) ends the activity.
  // Left set, `switchable` -- which requires an active session -- kept the submit button
  // disabled on the ordinary start form until the card remounted.
  //
  // Reset during render, not in an effect. An effect runs after the commit that showed the
  // new session, so for that gap the card displayed "running" or "completed" beside a stale
  // switch form, and a click on "Start a different session" landing in it was undone when
  // the effect caught up. That gap is what made sessionCockpitFlow fail about half the time
  // under a loaded test run. React's "adjusting state when a prop changes" pattern closes it.
  const switchScope = `${sessionId ?? ""}|${sessionActive}`;
  const [switchScopeSeen, setSwitchScopeSeen] = useState(switchScope);
  if (switchScope !== switchScopeSeen) {
    setSwitchScopeSeen(switchScope);
    setSwitching(false);
  }
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
    setDraftFocusMode(mode);
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
      <div className="card-header session-card-header">
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
          <div className="session-live-overview">
            <div className="session-live-goal">
              <p className="metric-label">Working on</p>
              <p className="metric-value">{sessionRecord?.goal || "--"}</p>
              <p className="session-live-mode">
                {FOCUS_MODE_LABELS[normalizeFocusMode(sessionRecord?.focusMode || focusMode)]} mode
              </p>
            </div>
            <div className="session-live-timer">
              <p className="metric-label">Elapsed</p>
              <p className="metric-value" aria-label="Elapsed session time">
                {formatElapsed(sessionRecord?.startedAtMs, nowMs)}
              </p>
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
              onClick={() => {
                if (switching) cancelSwitch();
                setSwitching(!switching);
              }}
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
          <div className="session-start-fields">
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
                // Roadmap 10.3. The arrow keys already moved a highlight through the
                // suggestions, but nothing told assistive tech which one, or that a list was
                // open: the rest of the ARIA combobox pattern the list half was built for.
                role="combobox"
                aria-autocomplete="list"
                aria-expanded={suggestionsOpen}
                aria-controls={suggestionsOpen ? "goal-suggestions-list" : undefined}
                aria-activedescendant={
                  suggestionsOpen && highlightedIndex >= 0
                    ? `goal-suggestion-${highlightedIndex}`
                    : undefined
                }
                disabled={sessionPending}
              />
              {suggestionsOpen && (
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
                        {suggestion.source === "pinned" ? "Pinned" : "Recent"} ·{" "}
                        {FOCUS_MODE_LABELS[suggestion.focusMode]}
                      </span>
                    </li>
                  ))}
                </ul>
              )}
            </div>
          </label>

          <label className="field">
            <span>Focus mode</span>
            <select
              value={focusMode}
              onChange={(event) => setDraftFocusMode(event.target.value as FocusMode)}
              disabled={sessionPending}
              aria-describedby="focus-mode-hint"
            >
              {FOCUS_MODES.map((mode) => (
                <option key={mode} value={mode}>
                  {FOCUS_MODE_LABELS[mode]}
                </option>
              ))}
            </select>
          </label>
          </div>
          {validation.message && (
            <p className="helper-text helper-error" id="session-goal-error" role="alert">
              {validation.message}
            </p>
          )}
          <p className="helper-text" id="focus-mode-hint">
            {FOCUS_MODE_HINT}
          </p>

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
                      {preset.goal} · {FOCUS_MODE_LABELS[preset.focusMode]}
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

    </section>
  );
});
