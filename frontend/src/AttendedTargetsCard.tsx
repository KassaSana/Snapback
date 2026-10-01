import { memo, useState } from "react";

import type { AttendedProgress } from "./api";
import { formatLiveTodayMins } from "./liveAttended";

// Opt-in attended-minute targets. Deliberately flat copy: two numbers and a ratio, no
// encouragement or streaks. Targets are on attendance, not the focus score.
type AttendedTargetsCardProps = {
  progress: AttendedProgress;
  onSave: (dailyMins: number, weeklyMins: number) => void | Promise<void>;
  /** One-line metric on a running Now. Edit still lives behind the same control. */
  compact?: boolean;
  /** True while attended time is accruing — unlocks the under-one-minute "<1m" label. */
  accruing?: boolean;
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
  compact = false,
  accruing = false,
}: AttendedTargetsCardProps) {
  const [editing, setEditing] = useState(false);
  const [daily, setDaily] = useState(String(progress.dailyTargetMins));
  const [weekly, setWeekly] = useState(String(progress.weeklyTargetMins));

  return (
    <section className={compact ? "attended-inline" : "card attended-targets-card"}>
      <div className="card-header">
        <h2>Attended time</h2>
        {compact ? null : <span className="pill">measured, not scored</span>}
      </div>

      {compact ? (
        <p className="attended-inline-line">
          Today {formatLiveTodayMins(progress.dailyActualMins, accruing)}
          {progress.dailyTargetMins > 0
            ? ` of ${formatMinutes(progress.dailyTargetMins)} planned`
            : ""}
        </p>
      ) : (
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
      )}

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
