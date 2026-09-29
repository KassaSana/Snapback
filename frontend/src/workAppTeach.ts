// First-session teaching: which windows were the work? Derives candidates from the timeline and
// remembers a durable skip; per-app "not now" is card state only.

import type { AppRuleRecord, ContextSnapshot } from "./api";
import { getAppRuleForName } from "./useAppRules";

// Key literal and its erase classification live in browserStorage.ts.
export { WORK_APP_TEACH_DONE_KEY } from "./browserStorage";
import { WORK_APP_TEACH_DONE_KEY } from "./browserStorage";

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
