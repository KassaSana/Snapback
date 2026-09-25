import { memo, useState } from "react";

import {
  REVIEW_RANGE_LABELS,
  REVIEW_RANGE_PRESETS,
  todayIsoDate,
  type ReviewRange,
  type ReviewRangePreset,
} from "./reviewRange";

type ReviewRangeBarProps = {
  disabled?: boolean;
  loading?: boolean;
  range: ReviewRange;
  onChange: (range: ReviewRange) => void;
  /**
   * Roadmap 10.11. The interval the cards below are actually showing, when it is not the
   * one selected: a load in progress, or one that failed. Said here, once, so the cards can
   * keep the honest label and the user can see why it differs from the pressed button.
   */
  showingLabel?: string | null;
  /** The last load failed; the cards hold the previous interval. */
  error?: string | null;
  onRetry?: () => void;
};

export const ReviewRangeBar = memo(function ReviewRangeBar({
  disabled = false,
  loading = false,
  range,
  onChange,
  showingLabel = null,
  error = null,
  onRetry,
}: ReviewRangeBarProps) {
  const [customDate, setCustomDate] = useState(
    range.preset === "custom" ? range.since : todayIsoDate(),
  );
  const activePreset: ReviewRangePreset =
    range.preset === "custom" ? "custom" : range.preset;

  return (
    <section className="card review-range-bar" aria-busy={loading}>
      <div className="card-header">
        <h2>Time range</h2>
        {loading ? <span className="pill">Updating…</span> : null}
      </div>
      <div className="review-range-controls" role="group" aria-label="Review time range">
        {REVIEW_RANGE_PRESETS.map((preset) => (
          <button
            key={preset}
            type="button"
            className={`secondary-button review-range-button${
              activePreset === preset ? " review-range-button-active" : ""
            }`}
            disabled={disabled}
            aria-pressed={activePreset === preset}
            onClick={() => onChange({ preset })}
          >
            {REVIEW_RANGE_LABELS[preset]}
          </button>
        ))}
        <button
          type="button"
          className={`secondary-button review-range-button${
            activePreset === "custom" ? " review-range-button-active" : ""
          }`}
          disabled={disabled}
          aria-pressed={activePreset === "custom"}
          onClick={() => onChange({ preset: "custom", since: customDate })}
        >
          {REVIEW_RANGE_LABELS.custom}
        </button>
        {activePreset === "custom" ? (
          <label className="review-range-custom">
            <span className="field-label">Since</span>
            <input
              type="date"
              aria-label="Custom range start date"
              value={customDate}
              max={todayIsoDate()}
              disabled={disabled}
              onChange={(event) => {
                setCustomDate(event.target.value);
                if (event.target.value) {
                  onChange({ preset: "custom", since: event.target.value });
                }
              }}
            />
          </label>
        ) : null}
      </div>
      {error ? (
        <p className="helper-text alert" role="alert">
          {error}
          {showingLabel ? ` Still showing ${showingLabel}.` : ""}{" "}
          {onRetry ? (
            <button type="button" className="link-button" onClick={onRetry}>
              Retry
            </button>
          ) : null}
        </p>
      ) : showingLabel ? (
        <p className="helper-text" role="status">
          Showing {showingLabel} until this loads.
        </p>
      ) : null}
      <details className="review-range-help">
        <summary>How this range works</summary>
        <p className="helper-text">
          Summary, charts, and sessions use this range. A selected session’s insight and
          context describe that session. Presets are rolling windows; planned attendance uses
          the calendar period shown beside it.
        </p>
      </details>
    </section>
  );
});
