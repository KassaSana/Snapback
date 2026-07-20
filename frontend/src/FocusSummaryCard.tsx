import type { FocusSummary } from "./api";
import { Tile } from "./InsightsCard";

type FocusSummaryCardProps = {
  focusSummary: FocusSummary;
};

export function FocusSummaryCard({ focusSummary }: FocusSummaryCardProps) {
  const { sampleCount, avgFocusScore, peakFocusScore, distractedFraction, longestFocusStreak } =
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
          <Tile value={String(longestFocusStreak)} label="Focus streak" />
        </div>
      )}
    </section>
  );
}
