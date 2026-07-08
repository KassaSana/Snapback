import { formatTime, type SessionRecord } from "./api";
import { FOCUS_MODES, type FocusMode } from "./useSession";

type SessionControlCardProps = {
  focusMode: FocusMode;
  handleFocusModeChange: (mode: FocusMode) => void;
  handleStartSession: () => void;
  handleStopSession: () => void;
  sessionGoal: string;
  sessionId: string | null;
  sessionRecord: SessionRecord | null;
  sessionStatusLabel: string;
  setSessionGoal: (value: string) => void;
};

export function SessionControlCard({
  focusMode,
  handleFocusModeChange,
  handleStartSession,
  handleStopSession,
  sessionGoal,
  sessionId,
  sessionRecord,
  sessionStatusLabel,
  setSessionGoal,
}: SessionControlCardProps) {
  return (
    <section className="card session-card">
      <div className="card-header">
        <h2>Session Control</h2>
        <span className="session-status">{sessionStatusLabel}</span>
      </div>
      <label className="field">
        <span>Focus goal</span>
        <input
          type="text"
          placeholder="Ship the snapback overlay"
          value={sessionGoal}
          onChange={(event) => setSessionGoal(event.target.value)}
        />
      </label>
      <label className="field">
        <span>Focus mode</span>
        <select
          value={focusMode}
          onChange={(event) =>
            void handleFocusModeChange(event.target.value as FocusMode)
          }
        >
          {FOCUS_MODES.map((mode) => (
            <option key={mode} value={mode}>
              {mode}
            </option>
          ))}
        </select>
      </label>
      <div className="button-row">
        <button className="primary-button" onClick={handleStartSession}>
          Start session
        </button>
        <button className="secondary-button" onClick={handleStopSession}>
          Stop session
        </button>
      </div>
      <div className="meta">
        <div>
          <p className="meta-label">Session ID</p>
          <p className="meta-value">{sessionId || "--"}</p>
        </div>
        <div>
          <p className="meta-label">Started</p>
          <p className="meta-value">{formatTime(sessionRecord?.startedAt ?? null)}</p>
        </div>
        <div>
          <p className="meta-label">Ended</p>
          <p className="meta-value">{formatTime(sessionRecord?.endedAt ?? null)}</p>
        </div>
      </div>
    </section>
  );
}
