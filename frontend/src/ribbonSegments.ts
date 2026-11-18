// The session ribbon on Now: one segment per reading in the running session, coloured by what
// the reading said. Pure so the merge and the wording can be tested without a DOM.

import type { PredictionRecord } from "./api";
import { displayVerdict } from "./utils";

export type RibbonKind = "deep" | "productive" | "drift" | "unknown";

/** Readings arrive every few seconds; a long session keeps the most recent ones. */
export const RIBBON_CAP = 240;

export const RIBBON_KIND_LABELS: Record<RibbonKind, string> = {
  deep: "Deep work",
  productive: "Productive",
  drift: "Drifting",
  unknown: "Not sure",
};

/**
 * What one reading looks like on the ribbon. Goes through the same display rule as the
 * verdict line, so a quiet screen the hero calls "Not sure" is not painted as deep work.
 */
export function ribbonKind(record: PredictionRecord, goal?: string | null): RibbonKind {
  const display = displayVerdict(record, goal);
  if (display.uncertain) return "unknown";
  if (record.focusState === "DEEP_FOCUS") return "deep";
  if (record.focusState === "PRODUCTIVE") return "productive";
  if (display.level === "high" || display.level === "medium") return "drift";
  return "unknown";
}

/**
 * Fold new readings into the ribbon's list: only this session's, one per timestamp, oldest
 * first, capped. Readings from another session (the previous one, or a stale event) drop out.
 */
export function mergeReadings(
  current: PredictionRecord[],
  incoming: readonly (PredictionRecord | null | undefined)[],
  sessionId: string | null,
  cap: number = RIBBON_CAP,
): PredictionRecord[] {
  if (!sessionId) return current.length === 0 ? current : [];
  const byTime = new Map<number, PredictionRecord>();
  for (const record of current) {
    if (record.sessionId === sessionId) byTime.set(record.timestampMs, record);
  }
  let added = false;
  for (const record of incoming) {
    if (!record || record.sessionId !== sessionId) continue;
    if (byTime.get(record.timestampMs) !== record) added = true;
    byTime.set(record.timestampMs, record);
  }
  if (!added && byTime.size === current.length) return current;
  const merged = [...byTime.values()].sort((a, b) => a.timestampMs - b.timestampMs);
  return merged.length > cap ? merged.slice(merged.length - cap) : merged;
}

/** The ribbon's accessible name: the same information as the colours, in words. */
export function ribbonSummary(kinds: readonly RibbonKind[]): string {
  if (kinds.length === 0) return "No recent readings for this session";
  const counts = new Map<RibbonKind, number>();
  for (const kind of kinds) counts.set(kind, (counts.get(kind) ?? 0) + 1);
  const parts = (Object.keys(RIBBON_KIND_LABELS) as RibbonKind[])
    .filter((kind) => counts.has(kind))
    .map((kind) => `${RIBBON_KIND_LABELS[kind].toLowerCase()} ${counts.get(kind)}`);
  const noun = kinds.length === 1 ? "reading" : "readings";
  return `${kinds.length} recent ${noun} for this session: ${parts.join(", ")}`;
}
