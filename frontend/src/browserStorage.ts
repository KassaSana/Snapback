// Every `localStorage` key this app owns, and which side of the privacy line it falls on.
// Roadmap 8.15.
//
// The list exists because "Delete all activity" (**8.12**) promised to erase every app-owned
// copy of the user's activity, and one key was quietly outside that promise.
// `sessionCockpit.ts` justified keeping session presets in `localStorage` on the grounds that
// they "are derived from goals the database already holds", so losing them is a non-event.
// That argument is true right up until the database holds no goals — after which the presets
// are the **only** surviving copy of goal strings the user typed, which
// [ADR-0009](../../docs/adr/0009-local-first-threat-model.md) ranks second in sensitivity
// behind window titles. The erase left them behind.
//
// So each key is classified here, next to the key, and the classification is the thing that
// decides whether the erase touches it:
//
//   - `activity`  — a copy of something the user did or recorded. Deleted with their activity.
//   - `preference` — a setting or a "don't show me this again" flag. Erasing activity is not
//     resetting the app (the native side says as much in its `retained` list), so these stay.
//
// `tests/browserStorage.test.ts` fails if a `snapback.*` key appears anywhere in `src/`
// without a row here, so the next key added has to pick a side rather than defaulting to
// "survives the erase" by nobody thinking about it.

export type BrowserStorageClass = "activity" | "preference";

export type BrowserStorageKey = {
  key: string;
  classification: BrowserStorageClass;
  /** How to name it to the user when it is deleted. Only meaningful for `activity`. */
  label: string;
  /** Why it is on this side of the line. */
  why: string;
};

export const APPEARANCE_STORAGE_KEY = "snapback.appearance";
export const FIRST_RUN_ACK_KEY = "snapback.firstRunPermissionsAcknowledged";
export const ONBOARDING_DONE_KEY = "snapback.onboardingJourneyComplete";
export const REVIEW_RANGE_STORAGE_KEY = "snapback.reviewRange";
export const SESSION_PRESETS_KEY = "snapback.sessionPresets";
export const WORK_APP_TEACH_DONE_KEY = "snapback.workAppTeachComplete";

export const BROWSER_STORAGE_KEYS: readonly BrowserStorageKey[] = [
  {
    key: SESSION_PRESETS_KEY,
    classification: "activity",
    label: "your saved session presets",
    why: "Holds goal text the user typed. Once the database is cleared this is the only copy, so it is activity, not a convenience.",
  },
  {
    key: APPEARANCE_STORAGE_KEY,
    classification: "preference",
    why: "Theme choice. Says nothing about what the user worked on.",
    label: "",
  },
  {
    key: FIRST_RUN_ACK_KEY,
    classification: "preference",
    why: "A 'seen it' flag for the permissions wizard. Clearing it would re-run onboarding over a working install.",
    label: "",
  },
  {
    key: ONBOARDING_DONE_KEY,
    classification: "preference",
    why: "As above, for the onboarding journey.",
    label: "",
  },
  {
    key: WORK_APP_TEACH_DONE_KEY,
    classification: "preference",
    why: "As above, for the work-app teaching prompt.",
    label: "",
  },
  {
    key: REVIEW_RANGE_STORAGE_KEY,
    classification: "preference",
    why: "Which window Review last showed (a '7d' string). It names no app, goal, or title — it is the shape of the question, not the answer.",
    label: "",
  },
];

export const ACTIVITY_STORAGE_KEYS: readonly BrowserStorageKey[] = BROWSER_STORAGE_KEYS.filter(
  (entry) => entry.classification === "activity",
);

type RemovableStorage = Pick<Storage, "removeItem">;

const defaultStorage = (): RemovableStorage | null => {
  try {
    return globalThis.localStorage ?? null;
  } catch {
    // Accessing localStorage can throw (disabled storage, sandboxed frame), the same way
    // sessionCockpit.ts guards its own access.
    return null;
  }
};

/**
 * Remove every `activity` key, and report what was named so the caller can tell the user.
 *
 * Returns the labels it *attempted*, not the ones it proved gone: a `removeItem` that throws
 * has left a copy behind, and that key is left out of the returned list rather than claimed.
 * Saying "your presets were cleared" when they were not is the specific failure **8.12**
 * exists to prevent, and it does not become acceptable because this side of the line is small.
 */
export function clearActivityStorage(
  storage: RemovableStorage | null = defaultStorage(),
): string[] {
  if (!storage) return [];
  const cleared: string[] = [];
  for (const entry of ACTIVITY_STORAGE_KEYS) {
    try {
      storage.removeItem(entry.key);
      cleared.push(entry.label);
    } catch {
      // Leave it out of the report. The native deletion already succeeded; this is one
      // replica among several and the message must not overstate what happened to it.
    }
  }
  return cleared;
}
