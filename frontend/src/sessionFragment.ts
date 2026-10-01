/** Under one minute with no predictions — discardable fragment, excluded from Review aggregates. */
export const SESSION_FRAGMENT_MAX_SECS = 60;

export function isSessionFragment(recap: {
  durationSecs: number;
  sampleCount?: number | null;
}): boolean {
  return recap.durationSecs < SESSION_FRAGMENT_MAX_SECS && (recap.sampleCount ?? 0) === 0;
}
