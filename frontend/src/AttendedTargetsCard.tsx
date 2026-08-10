import { memo, useState } from "react";

import type { AttendedProgress } from "./api";

// Roadmap 2.19. Opt-in attended-minute targets, off until someone sets one.
//
// The copy here is deliberately flat. The item rules out guilt, forced streaks and
// notifications, so this states two numbers and their ratio and stops: no "you're behind",
// no encouragement, no streak. Attendance is measured from durable spans, so it is the one
// quantity here the user can plan against — a focus *score* is a model opinion and would make
// a target into a demand to please the classifier.
type AttendedTargetsCardProps = {
  progress: AttendedProgress;
  onSave: (dailyMins: number, weeklyMins: number) => void | Promise<void>;
};

const formatMinutes = (mins: number): string => {
  const hours = Math.floor(mins / 60);
  const rest = mins % 60;
  return hours > 0 ? `${hours}h ${rest}m` : `${rest}m`;
};

const Row = ({
  label,
  actual,
  target,
}: {
  label: string;
  actual: number;
  target: number;
}) => (
  <div className="metric">
    <p className="metric-label">{label}</p>
    <p className="metric-value">{formatMinutes(actual)}</p>
    {target > 0 ? (
      <p className="meta-sub">
        of {formatMinutes(target)} planned ({Math.round((actual / target) * 100)}%)
      </p>
    ) : (
      // No target set: the attendance is still worth showing, so the number stands alone
      // rather than being hidden behind an opt-in.
      <p className="meta-sub">no target set</p>
    )}
  </div>
);

export const AttendedTargetsCard = memo(function AttendedTargetsCard({
  progress,
  onSave,
}: AttendedTargetsCardProps) {
  const [editing, setEditing] = useState(false);
  const [daily, setDaily] = useState(String(progress.dailyTargetMins));
  const [weekly, setWeekly] = useState(String(progress.weeklyTargetMins));

  return (
    <section className="card attended-targets-card">
      <div className="card-header">
        <h2>Attended time</h2>
        <span className="pill">measured, not scored</span>
      </div>

      <div className="metrics">
        <Row
          label="Today"
          actual={progress.dailyActualMins}
          target={progress.dailyTargetMins}
        />
        <Row
          label="This week"
          actual={progress.weeklyActualMins}
          target={progress.weeklyTargetMins}
        />
      </div>

      {editing ? (
        <>
          <label className="field-label" htmlFor="attended-daily">
            Daily target (minutes, 0 for none)
          </label>
          <input
            id="attended-daily"
            className="text-input"
            type="number"
            min={0}
            value={daily}
            onChange={(event) => setDaily(event.target.value)}
          />
          <label className="field-label" htmlFor="attended-weekly">
            Weekly target (minutes, 0 for none)
          </label>
          <input
            id="attended-weekly"
            className="text-input"
            type="number"
            min={0}
            value={weekly}
            onChange={(event) => setWeekly(event.target.value)}
          />
          <div className="button-row">
            <button
              className="primary-button"
              onClick={() => {
                void onSave(Number(daily) || 0, Number(weekly) || 0);
                setEditing(false);
              }}
            >
              Save targets
            </button>
            <button className="ghost-button" onClick={() => setEditing(false)}>
              Cancel
            </button>
          </div>
        </>
      ) : (
        <button className="secondary-button" onClick={() => setEditing(true)}>
          {progress.dailyTargetMins > 0 || progress.weeklyTargetMins > 0
            ? "Edit targets"
            : "Set a target"}
        </button>
      )}
    </section>
  );
});
