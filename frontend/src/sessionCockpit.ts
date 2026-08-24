// Roadmap 2.11. The session cockpit's decision logic, kept pure and out of the component for
// the reason sessionStatus.ts and activityDeletion.ts are: a rule that is only reachable
// through a rendered card is a rule with no cheap local test.
//
// The item's four complaints are really one complaint. Start and Stop are always enabled, a
// blank goal silently does nothing, duplicate clicks issue duplicate requests, and the
// prominent running-session metadata is a UUID. In each case the card knows something it
// declines to say: that the goal is not usable yet, that a request is already in flight, that
// the interesting number is elapsed time rather than an id. So the fix is not new data, it is
// letting the card answer with what it already has.
//
// **Presets never auto-start.** ADR-0005 makes declaration explicit and manual, and a preset
// that begins recording on click would quietly repeal that. A preset fills the form; the user
// still presses Start. Everything here therefore returns the *proposed* goal and mode and
// never a "started" anything.

import type { SessionRecord, SessionSummary } from "./api";

export const FOCUS_MODES = ["deep", "normal", "recovery"] as const;
export type FocusMode = (typeof FOCUS_MODES)[number];

/** Coerce an arbitrary backend/stored string to a focus mode, falling back rather than throwing. */
export const normalizeFocusMode = (
  value: string | null | undefined,
  fallback: FocusMode = "normal",
): FocusMode => {
  const mode = String(value ?? "").toLowerCase();
  return FOCUS_MODES.includes(mode as FocusMode) ? (mode as FocusMode) : fallback;
};

// A goal longer than this is almost always a pasted paragraph. The cap exists so the value
// stays readable in the running-session header and in Review, not for storage reasons.
export const MAX_GOAL_LENGTH = 200;

export type GoalValidation = {
  /** Whether Start may be pressed. */
  valid: boolean;
  /** Inline text shown under the field; null when there is nothing to say. */
  message: string | null;
};

/**
 * Validate the typed goal.
 *
 * `pristine` is the difference between "you have not typed anything yet" and "you cleared
 * this field". Scolding an untouched form on first paint is how a validation message teaches
 * the user to ignore validation messages, so an empty pristine field is quietly invalid: Start
 * is disabled, but nothing is red.
 */
export function validateSessionGoal(goal: string, pristine = false): GoalValidation {
  const trimmed = goal.trim();
  if (!trimmed) {
    return {
      valid: false,
      message: pristine ? null : "Name what you're working on before starting.",
    };
  }
  if (trimmed.length > MAX_GOAL_LENGTH) {
    return {
      valid: false,
      message: `Keep the goal under ${MAX_GOAL_LENGTH} characters (currently ${trimmed.length}).`,
    };
  }
  return { valid: true, message: null };
}

export type RecentGoal = {
  goal: string;
  focusMode: FocusMode;
};

/**
 * The distinct goals from recent history, newest first.
 *
 * History arrives newest-first from `get_session_history`, and the mode carried along is the
 * one that goal was *last* run with — repeating "Ship the overlay" should bring back the deep
 * mode it was last run in, not whatever the form happens to be showing.
 *
 * Matching is case- and whitespace-insensitive so "ship the overlay" and "Ship the overlay "
 * do not both occupy a slot, but the newest spelling is the one displayed: it is what the user
 * most recently chose to call the work.
 */
export function recentGoals(history: SessionSummary[], limit = 5): RecentGoal[] {
  const seen = new Set<string>();
  const out: RecentGoal[] = [];
  for (const summary of history ?? []) {
    const record = summary?.record;
    const goal = String(record?.goal ?? "").trim();
    if (!goal) continue;
    const key = goal.toLowerCase();
    if (seen.has(key)) continue;
    seen.add(key);
    out.push({ goal, focusMode: normalizeFocusMode(record?.focusMode) });
    if (out.length >= limit) break;
  }
  return out;
}

/** The most recent goal, for the one-click **Repeat last**; null when there is no history. */
export function lastSessionGoal(history: SessionSummary[]): RecentGoal | null {
  return recentGoals(history, 1)[0] ?? null;
}

export type GoalSuggestion = {
  goal: string;
  focusMode: FocusMode;
  source: "pinned" | "recent";
};

/**
 * Filters and ranks goal suggestions from pinned presets and recent history against a typed query.
 * Pinned presets take precedence, followed by distinct recent goals.
 */
export function filterGoalSuggestions(
  recent: RecentGoal[],
  presets: SessionPreset[],
  query = "",
  limit = 6,
): GoalSuggestion[] {
  const q = query.trim().toLowerCase();
  const seen = new Set<string>();
  const out: GoalSuggestion[] = [];

  // Pinned presets first
  for (const preset of presets ?? []) {
    const goal = preset.goal.trim();
    if (!goal) continue;
    const key = goal.toLowerCase();
    if (seen.has(key)) continue;
    if (!q || key.includes(q)) {
      seen.add(key);
      out.push({ goal, focusMode: preset.focusMode, source: "pinned" });
      if (out.length >= limit) return out;
    }
  }

  // Recent goals next
  for (const item of recent ?? []) {
    const goal = item.goal.trim();
    if (!goal) continue;
    const key = goal.toLowerCase();
    if (seen.has(key)) continue;
    if (!q || key.includes(q)) {
      seen.add(key);
      out.push({ goal, focusMode: item.focusMode, source: "recent" });
      if (out.length >= limit) return out;
    }
  }

  return out;
}

export type SessionPreset = {
  id: string;
  goal: string;
  focusMode: FocusMode;
};


export const SESSION_PRESETS_KEY = "snapback.sessionPresets";

type StorageLike = Pick<Storage, "getItem" | "setItem">;

const defaultStorage = (): StorageLike | null => {
  try {
    return globalThis.localStorage ?? null;
  } catch {
    // Accessing localStorage can throw (disabled storage, sandboxed frame).
    return null;
  }
};

/** Drop anything that is not a usable preset rather than rendering a half-parsed one. */
const coercePreset = (raw: unknown): SessionPreset | null => {
  if (!raw || typeof raw !== "object") return null;
  const source = raw as Record<string, unknown>;
  const goal = String(source.goal ?? "").trim();
  if (!goal || goal.length > MAX_GOAL_LENGTH) return null;
  const id = String(source.id ?? "").trim();
  return {
    id: id || `preset-${goal.toLowerCase()}`,
    goal,
    focusMode: normalizeFocusMode(String(source.focusMode ?? "")),
  };
};

/**
 * Read pinned presets. Any failure yields an empty list.
 *
 * Presets are a local convenience, not user data — they are derived from goals the database
 * already holds — so `localStorage` is the right home and losing them is a non-event. That is
 * also why a corrupt value is silently discarded instead of surfacing an error the user can do
 * nothing about.
 */
export function readSessionPresets(
  storage: StorageLike | null = defaultStorage(),
): SessionPreset[] {
  if (!storage) return [];
  try {
    const raw = storage.getItem(SESSION_PRESETS_KEY);
    if (!raw) return [];
    const parsed: unknown = JSON.parse(raw);
    if (!Array.isArray(parsed)) return [];
    return parsed.map(coercePreset).filter((entry): entry is SessionPreset => entry !== null);
  } catch {
    return [];
  }
}

/** Persist pinned presets; a storage failure costs the pin, never the session. */
export function writeSessionPresets(
  presets: SessionPreset[],
  storage: StorageLike | null = defaultStorage(),
): void {
  if (!storage) return;
  try {
    storage.setItem(SESSION_PRESETS_KEY, JSON.stringify(presets));
  } catch {
    // Quota or disabled storage. Worst case the pin does not survive a restart.
  }
}

/**
 * Pin a goal + mode.
 *
 * Pinning the same goal twice is a no-op on the list's *shape* but does update the mode, so
 * re-pinning after switching to deep mode corrects the preset instead of creating a duplicate
 * that differs only in a field the user cannot see.
 */
export function addSessionPreset(
  presets: SessionPreset[],
  goal: string,
  focusMode: FocusMode,
): SessionPreset[] {
  const trimmed = goal.trim();
  if (!validateSessionGoal(trimmed).valid) return presets;
  const key = trimmed.toLowerCase();
  const existing = presets.findIndex((preset) => preset.goal.trim().toLowerCase() === key);
  if (existing >= 0) {
    const updated = [...presets];
    updated[existing] = { ...updated[existing], goal: trimmed, focusMode };
    return updated;
  }
  return [...presets, { id: `preset-${key}-${presets.length}`, goal: trimmed, focusMode }];
}

export function removeSessionPreset(presets: SessionPreset[], id: string): SessionPreset[] {
  return presets.filter((preset) => preset.id !== id);
}

/**
 * Move a preset one slot up or down.
 *
 * Reordering is exposed as two buttons rather than drag-and-drop on purpose: a keyboard user
 * can reach it, and it is testable without synthesising pointer gestures. An out-of-range move
 * returns the list unchanged so the end buttons are harmless rather than needing to be hidden.
 */
export function moveSessionPreset(
  presets: SessionPreset[],
  id: string,
  direction: "up" | "down",
): SessionPreset[] {
  const index = presets.findIndex((preset) => preset.id === id);
  if (index < 0) return presets;
  const target = direction === "up" ? index - 1 : index + 1;
  if (target < 0 || target >= presets.length) return presets;
  const reordered = [...presets];
  [reordered[index], reordered[target]] = [reordered[target], reordered[index]];
  return reordered;
}

/**
 * Elapsed wall-clock time for a running session, formatted for the header.
 *
 * The origin is always the backend's `started_at`; `nowMs` is only the tick. That ordering is
 * the item's requirement and it matters after a sleep or a window reopen, when a browser-side
 * counter would resume from wherever it stopped and under-report the session by exactly the
 * time the user was away.
 *
 * This is *elapsed* time, deliberately not attended time: 7.23's spans are the source for the
 * latter, and inventing a second answer here is how two numbers that must agree stop agreeing.
 */
export function formatElapsed(startedAtMs: number | null | undefined, nowMs: number): string {
  if (startedAtMs === null || startedAtMs === undefined) return "--";
  const started = startedAtMs;
  if (Number.isNaN(started) || !Number.isFinite(nowMs)) return "--";
  const seconds = Math.floor((nowMs - started) / 1000);
  // A clock that disagrees with the backend by a second must not render "-1s".
  if (seconds < 0) return "0m 00s";
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const secs = seconds % 60;
  const pad = (value: number) => String(value).padStart(2, "0");
  return hours > 0 ? `${hours}h ${pad(minutes)}m` : `${minutes}m ${pad(secs)}s`;
}

/**
 * Whether Start may fire right now.
 *
 * The `pending` half is the duplicate-click fix. The click handler is async, so between the
 * first click and the state update that disables the button there is a real window in which a
 * second click issues a second `start_session`. Disabling on `pending` closes it in the UI;
 * useSession closes it again with a ref, because a disabled button is a courtesy and not a
 * guarantee (Enter on a focused form, a synthetic click, a slow render).
 */
export function canStartSession(goal: string, pending: boolean, sessionActive: boolean): boolean {
  if (pending || sessionActive) return false;
  return validateSessionGoal(goal).valid;
}

/** Whether Stop may fire: only with a live session and no request already in flight. */
export function canStopSession(record: SessionRecord | null, pending: boolean): boolean {
  if (pending || !record) return false;
  return record.status === "ACTIVE";
}
