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

/**
 * The headline the Privacy card shows.
 *
 * `alsoCleared` names browser-side copies the native result cannot know about (Roadmap 8.15):
 * `localStorage` belongs to this side of the bridge, so the native side reports nothing about
 * it, and an erase that silently left goal text behind in it was the defect. They are named
 * rather than merely deleted, because "all activity" is a claim the user has to be able to
 * check against what they can still see in the app.
 */
export function activityDeletionMessage(
  result: ActivityDeletionResult,
  alsoCleared: readonly string[] = [],
): string {
  const also = alsoCleared.length > 0 ? `, including ${joinList(alsoCleared)}` : "";
  if (result.complete) {
    return `All locally collected activity data was deleted${also}.`;
  }
  const count = result.failed.length;
  return (
    `Most of your activity data was deleted${also}, but ${count} ` +
    `${count === 1 ? "item" : "items"} could not be removed. ` +
    `Your recorded sessions are gone; these copies remain: ${result.failed.join("; ")}.`
  );
}

/** "a", "a and b", "a, b and c" -- so the sentence above reads as a sentence. */
function joinList(items: readonly string[]): string {
  if (items.length <= 1) return items[0] ?? "";
  return `${items.slice(0, -1).join(", ")} and ${items[items.length - 1]}`;
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
