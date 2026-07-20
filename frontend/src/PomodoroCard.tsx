import { formatPomodoroRemaining, type PomodoroStatus } from "./api";

type PomodoroCardProps = {
  pomodoroStatus: PomodoroStatus;
  sessionActive: boolean;
  onStart: () => void;
  onStop: () => void;
};

const PHASE_LABELS: Record<PomodoroStatus["phase"], string> = {
  work: "Work",
  shortBreak: "Short break",
  longBreak: "Long break",
};

export function PomodoroCard({ pomodoroStatus, sessionActive, onStart, onStop }: PomodoroCardProps) {
  const { running, phase, completedWorkIntervals, remainingMs } = pomodoroStatus;

  return (
    <section className="card pomodoro-card">
      <div className="card-header">
        <h2>Pomodoro</h2>
        <span className="pill">
          {completedWorkIntervals} completed
        </span>
      </div>

      <div className="metrics">
        <div className="metric">
          <p className="metric-label">Phase</p>
          <p className="metric-value">{PHASE_LABELS[phase]}</p>
        </div>
        <div className="metric">
          <p className="metric-label">Remaining</p>
          <p className="metric-value">
            {running ? formatPomodoroRemaining(remainingMs) : "--:--"}
          </p>
        </div>
      </div>

      {!sessionActive ? (
        <p className="helper-text">Start a focus session to use the Pomodoro timer.</p>
      ) : running ? (
        <button className="secondary-button" onClick={onStop}>
          Stop Pomodoro
        </button>
      ) : (
        <button className="primary-button" onClick={onStart}>
          Start Pomodoro
        </button>
      )}
    </section>
  );
}
