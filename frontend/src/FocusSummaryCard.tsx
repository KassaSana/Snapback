import { memo } from "react";

import type { FocusSummary } from "./api";
import { Tile } from "./InsightsCard";
import {
  FOCUS_STRETCH_LABEL,
  focusStretchHelperText,
  formatFocusStretch,
} from "./focusStreak";

type FocusSummaryCardProps = {
  focusSummary: FocusSummary;
};

export const FocusSummaryCard = memo(function FocusSummaryCard({ focusSummary }: FocusSummaryCardProps) {
  const { sampleCount, avgFocusScore, peakFocusScore, distractedFraction, longestFocusSecs } =
    focusSummary;

  return (
    <section className="card insights-card">
      <div className="card-header">
        <h2>Recent Focus</h2>
        <span className="pill">
          last {sampleCount} sample{sampleCount === 1 ? "" : "s"}
        </span>
      </div>

      {sampleCount === 0 ? (
        <p className="helper-text">
          No predictions recorded yet. Start a session to see recent focus trends here.
        </p>
      ) : (
        <div className="insight-tiles">
          <Tile value={String(Math.round(avgFocusScore))} label="Avg focus" />
          <Tile value={String(Math.round(peakFocusScore))} label="Peak focus" />
          <Tile value={`${Math.round(distractedFraction * 100)}%`} label="Distracted" />
          {/* Roadmap 10.13. A duration under a duration label. This tile used to show a
              count of prediction rows called "Focus streak". */}
          <Tile value={formatFocusStretch(longestFocusSecs)} label={FOCUS_STRETCH_LABEL} />
        </div>
      )}
      {sampleCount === 0 ? null : (
        <p className="helper-text">{focusStretchHelperText(longestFocusSecs)}</p>
      )}
    </section>
  );
});
