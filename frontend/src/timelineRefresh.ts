export const EVENT_TIMELINE_REFRESH_MS = 5_000;

export const shouldRefreshTimelineFromEvent = (
  lastRefreshAtMs: number | null,
  nowMs: number,
  minIntervalMs = EVENT_TIMELINE_REFRESH_MS,
) => {
  if (lastRefreshAtMs === null) {
    return true;
  }

  return nowMs - lastRefreshAtMs >= minIntervalMs;
};
