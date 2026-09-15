/** Roadmap 10.11. One Review-level time range drives every card on that surface so the
 *  user is never comparing numbers from different populations. The presets are rolling
 *  windows ("the last 7 x 24 hours") by design — 7.16 (integer-ms time) made calendar
 *  semantics possible, and the daily-summary series is the one query that snaps to local
 *  midnights; the scalar cards keep the rolling cutoff they have always had. */

export type ReviewRangePreset = "today" | "7d" | "30d" | "all" | "custom";

export type ReviewRange =
  | { preset: Exclude<ReviewRangePreset, "custom"> }
  | { preset: "custom"; since: string };

export type ReviewWindowRequest = {
  window: string;
  since?: string;
};

export const REVIEW_RANGE_STORAGE_KEY = "snapback.reviewRange";

export const REVIEW_RANGE_PRESETS: Array<Exclude<ReviewRangePreset, "custom">> = [
  "today",
  "7d",
  "30d",
  "all",
];

// The labels say "Last …" because that is what the scalar queries compute: "today" is the
// last 24 hours, not the calendar day, and "7d" is 7 x 24 hours, not this week. Attendance is
// the one card that snaps to the calendar, and SummaryCard says so beside its figure rather
// than letting one pill imply that every number on the surface shares a period.
export const REVIEW_RANGE_LABELS: Record<ReviewRangePreset, string> = {
  today: "Last 24h",
  "7d": "Last 7 days",
  "30d": "Last 30 days",
  all: "All time",
  custom: "Custom",
};

export function reviewRangeLabel(range: ReviewRange): string {
  if (range.preset === "custom") {
    return `Since ${range.since}`;
  }
  return REVIEW_RANGE_LABELS[range.preset];
}

// The date the user picked is a local calendar day, so its start is local midnight, not UTC
// midnight: `${since}T00:00:00Z` was up to a day off in either direction depending on the
// zone. `new Date("YYYY-MM-DDT00:00:00")` (no zone suffix) is local time by spec.
export function customRangeSinceInstant(since: string): string {
  const local = new Date(`${since}T00:00:00`);
  return Number.isNaN(local.getTime()) ? `${since}T00:00:00Z` : local.toISOString();
}

export function toReviewWindowRequest(range: ReviewRange): ReviewWindowRequest {
  if (range.preset === "custom") {
    return { window: "custom", since: customRangeSinceInstant(range.since) };
  }
  if (range.preset === "today") return { window: "day" };
  return { window: range.preset };
}

export function readStoredReviewRange(): ReviewRange {
  if (typeof globalThis.localStorage === "undefined") return { preset: "7d" };
  try {
    const raw = globalThis.localStorage.getItem(REVIEW_RANGE_STORAGE_KEY);
    if (!raw) return { preset: "7d" };
    const parsed = JSON.parse(raw) as ReviewRange;
    if (parsed.preset === "custom" && typeof parsed.since === "string" && parsed.since) {
      return parsed;
    }
    if (REVIEW_RANGE_PRESETS.includes(parsed.preset as (typeof REVIEW_RANGE_PRESETS)[number])) {
      return { preset: parsed.preset as Exclude<ReviewRangePreset, "custom"> };
    }
  } catch {
    // Corrupt storage falls back to the default rather than breaking Review.
  }
  return { preset: "7d" };
}

export function writeStoredReviewRange(range: ReviewRange): void {
  if (typeof globalThis.localStorage === "undefined") return;
  globalThis.localStorage.setItem(REVIEW_RANGE_STORAGE_KEY, JSON.stringify(range));
}

/** ISO calendar date (YYYY-MM-DD) for the custom-range picker. */
export function todayIsoDate(): string {
  const now = new Date();
  const month = String(now.getMonth() + 1).padStart(2, "0");
  const day = String(now.getDate()).padStart(2, "0");
  return `${now.getFullYear()}-${month}-${day}`;
}
