// Every localStorage key this app owns, and which side of the privacy line it falls on:
//
//   - `activity`   a copy of something the user did or typed; deleted with their activity.
//   - `preference` a setting or "don't show again" flag; kept, like native configuration.
//
// Session presets are activity: once the database holds no goals they are the only copy of goal
// strings the user typed. tests/browserStorage.test.ts fails if a `snapback.*` key in src/ has
// no row here.

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
 * Remove every `activity` key and return the labels actually removed; a key whose removeItem
 * threw is left out rather than claimed as cleared.
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
