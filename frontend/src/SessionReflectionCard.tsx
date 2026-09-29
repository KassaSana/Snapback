import { memo, useState } from "react";

// Two optional questions at the end of a session. Skip writes nothing, so skipped and never
// asked are the same state.
type SessionReflectionCardProps = {
  onSave: (done: string | null, nextStep: string | null) => void | Promise<void>;
  onSkip: () => void;
  saved: boolean;
};

const MAX_LENGTH = 1000;

export const SessionReflectionCard = memo(function SessionReflectionCard({
  onSave,
  onSkip,
  saved,
}: SessionReflectionCardProps) {
  const [done, setDone] = useState("");
  const [nextStep, setNextStep] = useState("");

  // Blank is not an answer. Mirrors the backend, which trims and treats whitespace as unset,
  // so the button cannot promise a save that would store nothing.
  const hasAnswer = done.trim().length > 0 || nextStep.trim().length > 0;

  if (saved) {
    return (
      <section className="card reflection-card">
        <div className="card-header">
          <h2>Reflection</h2>
          <span className="pill">saved</span>
        </div>
        <p className="helper-text">
          Kept with the session, and included when you export your data.
        </p>
      </section>
    );
  }

  return (
    <section className="card reflection-card">
      <div className="card-header">
        <h2>Reflection</h2>
        <span className="pill">optional</span>
      </div>
      <p className="helper-text">
        Two lines now make tomorrow&apos;s restart easier. This is for you — it is never used to
        train the model.
      </p>

      <label className="field-label" htmlFor="reflection-done">
        What got done?
      </label>
      <textarea
        id="reflection-done"
        className="text-input"
        rows={2}
        maxLength={MAX_LENGTH}
        value={done}
        onChange={(event) => setDone(event.target.value)}
      />

      <label className="field-label" htmlFor="reflection-next">
        Next step
      </label>
      <textarea
        id="reflection-next"
        className="text-input"
        rows={2}
        maxLength={MAX_LENGTH}
        value={nextStep}
        onChange={(event) => setNextStep(event.target.value)}
      />

      <div className="button-row">
        <button
          className="primary-button"
          disabled={!hasAnswer}
          onClick={() => void onSave(done.trim() || null, nextStep.trim() || null)}
        >
          Save reflection
        </button>
        <button className="ghost-button" onClick={onSkip}>
          Skip
        </button>
      </div>
    </section>
  );
});
