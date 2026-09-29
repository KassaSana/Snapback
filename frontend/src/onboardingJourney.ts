// The guided continuation after the permission wizard: choose a goal, start, read a verdict,
// correct it, stop, read the recap. It only observes: every step advances because app state
// changed, and it issues no commands, so it is safe to repeat. Only completion is persisted.

export const ONBOARDING_STEPS = [
  "goal",
  "start",
  "verdict",
  "correct",
  "stop",
  "review",
] as const;

export type OnboardingStep = (typeof ONBOARDING_STEPS)[number];

// Key literal and its erase classification live in browserStorage.ts.
export { ONBOARDING_DONE_KEY } from "./browserStorage";
import { ONBOARDING_DONE_KEY } from "./browserStorage";

export type OnboardingCopy = {
  title: string;
  detail: string;
};

export const ONBOARDING_COPY: Record<OnboardingStep, OnboardingCopy> = {
  goal: {
    title: "Name what you're working on",
    detail:
      "A session is something you declare. Type a goal in Session Control — it is the label the rest of the app hangs off.",
  },
  start: {
    title: "Start the session",
    detail: "Snapback only records while a session is running. Nothing before this is kept.",
  },
  verdict: {
    title: "Wait for your first reading",
    detail:
      "Work normally for a moment. Snapback reads the active window and says whether it looks like focused work.",
  },
  correct: {
    title: "Correct it if it's wrong",
    detail:
      "The first reading is a guess from a heuristic. Telling it what the moment actually was is how it learns your work.",
  },
  stop: {
    title: "Stop the session",
    detail: "Stopping writes the recap and asks how the session felt overall.",
  },
  review: {
    title: "Read how the session went",
    detail: "This is the payoff: what the session actually contained, in your own terms.",
  },
};

export type OnboardingState = {
  /** 1.1's finish line. Nothing here runs until capture actually works. */
  captureReady: boolean;
  /** A non-blank goal is typed into the cockpit. */
  goalEntered: boolean;
  /** A session is running right now. */
  sessionActive: boolean;
  /** At least one prediction has arrived this run. */
  predictionSeen: boolean;
  /** The user has submitted or deliberately skipped a correction. */
  feedbackGiven: boolean;
  /** A session has been stopped and its recap exists. */
  sessionCompleted: boolean;
  /** The user has opened the Review surface since that recap landed. */
  recapSeen: boolean;
};

/**
 * The step the user is on, or null when the journey is finished.
 *
 * Deliberately derived fresh from state on every render rather than advanced by a counter.
 * A counter would let the guide and the app disagree — the classic version of that bug is a
 * wizard still saying "now start a session" over a running one, because the user did the step
 * before the wizard asked.
 *
 * Order matters and is not negotiable: each step's evidence is a superset of the ones before
 * it, so checking them in sequence means a user who did three steps at once lands on the
 * fourth rather than being walked back through work they already did.
 */
export function currentOnboardingStep(state: OnboardingState): OnboardingStep | null {
  if (!state.captureReady) return null;
  if (state.recapSeen) return null;
  // Everything below is "the earliest thing not yet true".
  if (!state.goalEntered && !state.sessionActive && !state.sessionCompleted) return "goal";
  if (!state.sessionActive && !state.sessionCompleted) return "start";
  if (!state.predictionSeen && !state.sessionCompleted) return "verdict";
  if (!state.feedbackGiven && !state.sessionCompleted) return "correct";
  if (!state.sessionCompleted) return "stop";
  return "review";
}

/** How far along the journey is, for a "step 3 of 6" readout. */
export function onboardingProgress(step: OnboardingStep | null): {
  index: number;
  total: number;
} {
  const total = ONBOARDING_STEPS.length;
  if (!step) return { index: total, total };
  return { index: ONBOARDING_STEPS.indexOf(step) + 1, total };
}

export type OnboardingFailure = {
  /** Copy explaining what is wrong in the user's terms. */
  message: string;
  /** The label of the control that routes to the recovery UI. */
  actionLabel: string;
};

/**
 * A blocking problem the guide hands off to the UI that owns it (Permissions, Privacy) rather
 * than growing its own remediation copy.
 */
export function onboardingFailure(input: {
  captureFailed: boolean;
  privateMode: boolean;
}): OnboardingFailure | null {
  if (input.captureFailed) {
    return {
      message:
        "Capture stopped, so this walkthrough is paused — there is nothing to read until it is running again.",
      actionLabel: "Open permissions",
    };
  }
  if (input.privateMode) {
    return {
      message:
        "Private mode is on, so nothing is being recorded. The walkthrough resumes when you turn it off.",
      actionLabel: "Open privacy settings",
    };
  }
  return null;
}

type StorageLike = Pick<Storage, "getItem" | "setItem" | "removeItem">;

const defaultStorage = (): StorageLike | null => {
  try {
    return globalThis.localStorage ?? null;
  } catch {
    return null;
  }
};

/** Whether the user has already finished or skipped the walkthrough. */
export function readOnboardingComplete(
  storage: StorageLike | null = defaultStorage(),
): boolean {
  if (!storage) return false;
  try {
    return storage.getItem(ONBOARDING_DONE_KEY) === "true";
  } catch {
    return false;
  }
}

/** Remember that it is done, so it does not reappear on the next launch. */
export function writeOnboardingComplete(
  storage: StorageLike | null = defaultStorage(),
): void {
  if (!storage) return;
  try {
    storage.setItem(ONBOARDING_DONE_KEY, "true");
  } catch {
    // Worst case the guide offers itself again; nothing is lost.
  }
}

/**
 * Clear the flag so the walkthrough can be replayed from Help.
 *
 * Resumable *and* repeatable, which only costs nothing because the guide observes rather than
 * acts: replaying it produces no second session, no duplicate label, and no new rows.
 */
export function clearOnboardingComplete(
  storage: StorageLike | null = defaultStorage(),
): void {
  if (!storage) return;
  try {
    storage.removeItem(ONBOARDING_DONE_KEY);
  } catch {
    // Ignore disabled storage.
  }
}

/** Whether to render the guide at all. */
export function shouldShowOnboarding(input: {
  captureReady: boolean;
  completed: boolean;
  step: OnboardingStep | null;
}): boolean {
  return input.captureReady && !input.completed && input.step !== null;
}
