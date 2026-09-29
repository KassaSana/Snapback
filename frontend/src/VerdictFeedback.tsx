import { useState } from "react";

import { focusStateLabel, type FocusLabel } from "./api";

// Quiet correction on Now. "Wrong?" asks for the right answer, so a disagreement becomes a
// labelled example.

const STATES: FocusLabel[] = ["DEEP_FOCUS", "PRODUCTIVE", "PSEUDO_PRODUCTIVE", "DISTRACTED"];

type Props = {
  disabled: boolean;
  onCorrect: (label: FocusLabel) => void;
  predictedState: string | null;
  status: string | null;
};

export function VerdictFeedback({
  disabled,
  onCorrect,
  predictedState,
  status,
}: Props) {
  const [correcting, setCorrecting] = useState(false);

  if (disabled) {
    return null;
  }

  return (
    <div className="verdict-feedback">
      {correcting ? (
        <>
          <p className="verdict-feedback-prompt" id="verdict-correction-label">
            What was it really?
          </p>
          <div className="verdict-options" role="group" aria-labelledby="verdict-correction-label">
            {STATES.filter((state) => state !== predictedState).map((state) => (
              <button
                key={state}
                type="button"
                className="verdict-option"
                onClick={() => {
                  onCorrect(state);
                  setCorrecting(false);
                }}
              >
                {focusStateLabel(state)}
              </button>
            ))}
            <button type="button" className="link-button" onClick={() => setCorrecting(false)}>
              Cancel
            </button>
          </div>
        </>
      ) : (
        <>
          <button
            type="button"
            className="link-button"
            onClick={() => setCorrecting(true)}
            aria-label="This reading is wrong"
          >
            Wrong?
          </button>
        </>
      )}
      {status ? (
        <p className="verdict-feedback-status" aria-live="polite">
          {status}
        </p>
      ) : null}
    </div>
  );
}
