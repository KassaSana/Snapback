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
};

export const ReviewRangeBar = memo(function ReviewRangeBar({
  disabled = false,
  loading = false,
  range,
  onChange,
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
      <p className="helper-text">
        Every card below uses this exact interval. Windows are rolling until calendar-day
        boundaries land in a future release.
      </p>
    </section>
  );
});
