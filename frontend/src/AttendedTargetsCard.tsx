import { memo, useState } from "react";

import type { AttendedProgress } from "./api";
import { formatLiveTodayMins } from "./liveAttended";

// Today's attended time on Now, with optional daily and weekly targets.
type AttendedTargetsCardProps = {
  progress: AttendedProgress;
  onSave: (dailyMins: number, weeklyMins: number) => void | Promise<void>;
  /** True while attended time is accruing — unlocks the under-one-minute "<1m" label. */
  accruing?: boolean;
};

const formatMinutes = (mins: number): string => {
  const hours = Math.floor(mins / 60);
  const rest = mins % 60;
  return hours > 0 ? `${hours}h ${rest}m` : `${rest}m`;
};

export const AttendedTargetsCard = memo(function AttendedTargetsCard({
  progress,
  onSave,
  accruing = false,
}: AttendedTargetsCardProps) {
  const [editing, setEditing] = useState(false);
  const [daily, setDaily] = useState(String(progress.dailyTargetMins));
  const [weekly, setWeekly] = useState(String(progress.weeklyTargetMins));

  return (
    <section className="attended-inline">
      <div className="card-header">
        <h2>Attended time</h2>
      </div>

      <p className="attended-inline-line">
        Today {formatLiveTodayMins(progress.dailyActualMins, accruing)}
        {progress.dailyTargetMins > 0 ? ` of ${formatMinutes(progress.dailyTargetMins)} planned` : ""}
      </p>

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
