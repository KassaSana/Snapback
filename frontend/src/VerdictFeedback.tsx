import { useState } from "react";

import { focusStateLabel, type FocusLabel } from "./api";

// Agreement capture on the Now surface.
//
// Why "Wrong" asks a follow-up instead of just recording a thumbs-down: a disagreement
// with no correct answer cannot train anything and cannot build a confusion matrix. One
// extra click turns a complaint into a labelled example. Roadmap 13.5 asks whether there
// is enough labelled data to train on at all — this is how that question gets an answer
// made of data instead of speculation.

const STATES: FocusLabel[] = ["DEEP_FOCUS", "PRODUCTIVE", "PSEUDO_PRODUCTIVE", "DISTRACTED"];

type Props = {
  disabled: boolean;
  onConfirm: () => void;
  onCorrect: (label: FocusLabel) => void;
  predictedState: string | null;
  status: string | null;
  /**
   * The hero is showing Settled rather than the stored verdict. "Is this right?"
   * would ask about a word we just refused to print; ask about the work instead.
   * Right still records agreement with the model's state — that is the label.
   */
  uncertain?: boolean;
};

export function VerdictFeedback({
  disabled,
  onConfirm,
  onCorrect,
  predictedState,
  status,
  uncertain = false,
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
          <span className="verdict-feedback-prompt">
            {uncertain ? "Were you actually working?" : "Is this right?"}
          </span>
          <button
            type="button"
            className="verdict-vote"
            onClick={onConfirm}
            aria-label="This reading is right"
          >
            👍 Right
          </button>
          <button
            type="button"
            className="verdict-vote"
            onClick={() => setCorrecting(true)}
            aria-label="This reading is wrong"
          >
            👎 Wrong
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
