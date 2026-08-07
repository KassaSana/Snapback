import { memo } from "react";

import type { FocusLabel } from "./api";

type FocusFeedbackCardProps = {
  handleLabel: (label: FocusLabel) => void | Promise<void>;
  labelStatus: string | null;
  labelStatusWarning: boolean;
};

// Consumer Settings card for labelling the current moment. ADR-0006: training/deploy lives
// elsewhere (developer tooling), so this card must not advertise a missing ml/ pipeline.
export const FocusFeedbackCard = memo(function FocusFeedbackCard({
  handleLabel,
  labelStatus,
  labelStatusWarning,
}: FocusFeedbackCardProps) {
  return (
    <section className="card feedback-card">
      <div className="card-header">
        <h2>Focus Feedback</h2>
        <span className="pill">label moments</span>
      </div>
      <p className="helper-text">
        Was that moment actually focused? Use these controls to label it while a session is
        active.
      </p>
      <div className="button-row feedback-row">
        <button className="secondary-button" onClick={() => void handleLabel("DEEP_FOCUS")}>
          Deep
        </button>
        <button className="secondary-button" onClick={() => void handleLabel("PRODUCTIVE")}>
          Focused
        </button>
        <button
          className="secondary-button"
          onClick={() => void handleLabel("PSEUDO_PRODUCTIVE")}
        >
          Drift
        </button>
        <button className="secondary-button" onClick={() => void handleLabel("DISTRACTED")}>
          Distracted
        </button>
      </div>
      {labelStatus ? (
        <p className={`helper-text${labelStatusWarning ? " alert" : ""}`}>{labelStatus}</p>
      ) : null}
    </section>
  );
});
