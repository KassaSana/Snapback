// First-session teaching: which windows were the work?
//
// Allow/Block rules are the accuracy lever the heuristic actually has, but they live in
// Settings. A quiet Deep-work guess stays a guess until the user names the apps that count.
// This module derives the candidate list from the session timeline and remembers a skip, so
// the card can be an observer of existing rows rather than a second rule store.
//
// Skip is durable (like the onboarding journey). Per-app "not now" is the card's own
// session state and is not persisted — a new unknown window should be offerable again.

import type { AppRuleRecord, ContextSnapshot } from "./api";
import { getAppRuleForName } from "./useAppRules";

export const WORK_APP_TEACH_DONE_KEY = "snapback.workAppTeachComplete";

export const WORK_APP_TEACH_LIMIT = 5;

export type WorkAppCandidate = {
  appName: string;
  sampleCount: number;
  exampleTitle: string;
};

export function workAppCandidates(
  timeline: Pick<ContextSnapshot, "appName" | "windowTitle">[],
  rules: AppRuleRecord[],
  limit: number = WORK_APP_TEACH_LIMIT,
): WorkAppCandidate[] {
  const byName = new Map<string, WorkAppCandidate>();

  for (const row of timeline) {
    const appName = row.appName.trim();
    if (!appName) continue;
    if (getAppRuleForName(rules, appName)) continue;

    const existing = byName.get(appName);
    if (existing) {
      existing.sampleCount += 1;
      if (!existing.exampleTitle && row.windowTitle.trim()) {
        existing.exampleTitle = row.windowTitle.trim();
      }
    } else {
      byName.set(appName, {
        appName,
        sampleCount: 1,
        exampleTitle: row.windowTitle.trim(),
      });
    }
  }

  return [...byName.values()]
    .sort((a, b) => b.sampleCount - a.sampleCount || a.appName.localeCompare(b.appName))
    .slice(0, Math.max(0, limit));
}

export function shouldShowWorkAppTeach(input: {
  dismissed: boolean;
  candidates: WorkAppCandidate[];
}): boolean {
  return !input.dismissed && input.candidates.length > 0;
}

type StorageLike = Pick<Storage, "getItem" | "setItem" | "removeItem">;

const defaultStorage = (): StorageLike | null => {
  try {
    return globalThis.localStorage ?? null;
  } catch {
    return null;
  }
};

export function readWorkAppTeachComplete(storage: StorageLike | null = defaultStorage()): boolean {
  if (!storage) return false;
  try {
    return storage.getItem(WORK_APP_TEACH_DONE_KEY) === "true";
  } catch {
    return false;
  }
}

export function writeWorkAppTeachComplete(storage: StorageLike | null = defaultStorage()): void {
  if (!storage) return;
  try {
    storage.setItem(WORK_APP_TEACH_DONE_KEY, "true");
  } catch {
    // Worst case the card offers itself again; nothing is lost.
  }
}

/** Clear the skip so the card can be replayed from Help, matching onboarding. */
export function clearWorkAppTeachComplete(storage: StorageLike | null = defaultStorage()): void {
  if (!storage) return;
  try {
    storage.removeItem(WORK_APP_TEACH_DONE_KEY);
  } catch {
    // Ignore disabled storage.
  }
}
