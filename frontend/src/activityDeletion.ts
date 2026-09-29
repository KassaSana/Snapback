// What to tell the user after "Delete all activity". Never say "permanently deleted" over a
// partial result: the native side reports each target, and a file held open elsewhere is a
// legitimate partial outcome.

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
 * The headline the Privacy card shows. `alsoCleared` names the browser-side copies (which the
 * native side cannot see), so "all activity" is checkable against what remains in the app.
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
