// Roadmap 8.12. What to tell the user after "Delete all activity".
//
// Pure, and in its own module, for the reason sessionStatus.ts and analyticsChart.ts are: the
// component suite cannot run on this machine (11.11), so a message that is only reachable
// through a rendered component has no local test. This one earns the separation twice over,
// because the wrong string here is a privacy claim rather than a cosmetic slip.
//
// The rule the item states outright: **never say "permanently deleted" over a partial result.**
// The native side attempts every target and reports each one, so a stale export held open by
// another program leaves the database cleared and one file behind. That is a legitimate
// outcome and it has to read as one.

export type ActivityDeletionResult = {
  deleted: string[];
  failed: string[];
  retained: string[];
  complete: boolean;
};

/** Tolerant of a missing or malformed payload: an unreadable result is not a clean one. */
export function mapActivityDeletionResult(raw: unknown): ActivityDeletionResult {
  const source = (raw ?? {}) as Record<string, unknown>;
  const list = (value: unknown): string[] =>
    Array.isArray(value) ? value.map((entry) => String(entry)) : [];
  const failed = list(source.failed);
  return {
    deleted: list(source.deleted),
    failed,
    retained: list(source.retained),
    // Derived from `failed` when the flag is absent rather than defaulting to true. A payload
    // we could not read must not be reported as a completed erasure.
    complete: typeof source.complete === "boolean" ? source.complete : failed.length === 0,
  };
}

/** The headline the Privacy card shows. */
export function activityDeletionMessage(result: ActivityDeletionResult): string {
  if (result.complete) {
    return "All locally collected activity data was deleted.";
  }
  const count = result.failed.length;
  return (
    `Most of your activity data was deleted, but ${count} ` +
    `${count === 1 ? "item" : "items"} could not be removed. ` +
    `Your recorded sessions are gone; these copies remain: ${result.failed.join("; ")}.`
  );
}

/** Whether that headline is good news, so the card can style it honestly. */
export function activityDeletionIsWarning(result: ActivityDeletionResult): boolean {
  return !result.complete;
}

/** The always-shown footnote naming what "delete activity" deliberately does not touch. */
export function activityDeletionRetainedNote(result: ActivityDeletionResult): string | null {
  if (result.retained.length === 0) return null;
  return `Kept, because erasing activity is not the same as resetting the app: ${result.retained.join("; ")}.`;
}
