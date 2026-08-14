import assert from "node:assert/strict";

import type { SessionSummary } from "../src/api";
import {
  MAX_GOAL_LENGTH,
  addSessionPreset,
  canStartSession,
  canStopSession,
  filterGoalSuggestions,
  formatElapsed,

  lastSessionGoal,
  moveSessionPreset,
  normalizeFocusMode,
  readSessionPresets,
  recentGoals,
  removeSessionPreset,
  validateSessionGoal,
  writeSessionPresets,
  type SessionPreset,
} from "../src/sessionCockpit";

import { MemoryStorage } from "./memoryStorage";

const summary = (goal: string, focusMode = "normal", status = "COMPLETED"): SessionSummary =>
  ({
    record: {
      sessionId: `id-${goal}`,
      goal,
      status,
      focusMode,
      startedAt: "2026-08-10T09:00:00Z",
      endedAt: "2026-08-10T10:00:00Z",
      reflectionDone: null,
      reflectionNextStep: null,
    },
    recap: {},
  }) as unknown as SessionSummary;

// ---------------------------------------------------------------------------
// Goal validation. THE RULE: a blank goal must never silently do nothing.
// ---------------------------------------------------------------------------

// An untouched form says nothing, but still cannot start. Those are two different questions
// and conflating them either nags on first paint or lets a blank session through.
assert.deepEqual(validateSessionGoal("", true), { valid: false, message: null });
assert.equal(validateSessionGoal("", false).valid, false);
assert.ok(validateSessionGoal("", false).message);

// Whitespace is not a goal. This is the exact input that used to no-op without explanation.
assert.equal(validateSessionGoal("   ").valid, false);
assert.ok(validateSessionGoal("   ").message?.includes("Name what you're working on"));

assert.deepEqual(validateSessionGoal("Ship the overlay"), { valid: true, message: null });
// Surrounding whitespace is trimmed before judging, so " x " is a real goal.
assert.equal(validateSessionGoal("  x  ").valid, true);

// The cap reports the actual length, so the message is actionable rather than a scold.
{
  const long = "a".repeat(MAX_GOAL_LENGTH + 5);
  const result = validateSessionGoal(long);
  assert.equal(result.valid, false);
  assert.ok(result.message?.includes(String(MAX_GOAL_LENGTH + 5)));
  // Exactly at the cap is allowed: an off-by-one here rejects a goal the message calls legal.
  assert.equal(validateSessionGoal("a".repeat(MAX_GOAL_LENGTH)).valid, true);
}

// ---------------------------------------------------------------------------
// Recent goals.
// ---------------------------------------------------------------------------

assert.deepEqual(recentGoals([]), []);
// Tolerates the shape it actually gets on a cold start rather than throwing into a render.
assert.deepEqual(recentGoals(undefined as unknown as SessionSummary[]), []);

{
  const history = [
    summary("Ship the overlay", "deep"),
    summary("Answer email", "normal"),
    summary("ship the overlay ", "recovery"),
    summary("   "),
    summary("Write the ADR", "deep"),
  ];

  const goals = recentGoals(history);
  // Deduped case-insensitively: the same work typed two ways is one entry...
  assert.equal(goals.length, 3);
  // ...and the newest spelling and mode win, because that is what the user last chose.
  assert.deepEqual(goals[0], { goal: "Ship the overlay", focusMode: "deep" });
  assert.deepEqual(goals[1], { goal: "Answer email", focusMode: "normal" });
  assert.deepEqual(goals[2], { goal: "Write the ADR", focusMode: "deep" });
  // A blank goal never becomes a clickable empty chip.
  assert.ok(goals.every((entry) => entry.goal.trim().length > 0));

  assert.equal(recentGoals(history, 2).length, 2);
  assert.deepEqual(lastSessionGoal(history), { goal: "Ship the overlay", focusMode: "deep" });
}

assert.equal(lastSessionGoal([]), null);
// An unknown mode from an older row degrades to normal instead of reaching a <select> that
// has no such option.
assert.deepEqual(lastSessionGoal([summary("x", "hyperfocus")]), {
  goal: "x",
  focusMode: "normal",
});
assert.equal(normalizeFocusMode("DEEP"), "deep");
assert.equal(normalizeFocusMode(null), "normal");
assert.equal(normalizeFocusMode("nonsense", "recovery"), "recovery");

// ---------------------------------------------------------------------------
// Pinned presets.
// ---------------------------------------------------------------------------

{
  let presets: SessionPreset[] = [];
  presets = addSessionPreset(presets, "Ship the overlay", "deep");
  presets = addSessionPreset(presets, "Answer email", "normal");
  assert.equal(presets.length, 2);

  // Re-pinning the same goal corrects the mode instead of creating an invisible duplicate.
  presets = addSessionPreset(presets, "ship the overlay", "recovery");
  assert.equal(presets.length, 2);
  assert.equal(presets[0].focusMode, "recovery");
  assert.equal(presets[0].goal, "ship the overlay");

  // A goal that would fail validation cannot be pinned either.
  assert.equal(addSessionPreset(presets, "   ", "deep").length, 2);
  assert.equal(addSessionPreset(presets, "a".repeat(MAX_GOAL_LENGTH + 1), "deep").length, 2);

  // Reordering.
  const ids = presets.map((preset) => preset.id);
  const moved = moveSessionPreset(presets, ids[1], "up");
  assert.deepEqual(
    moved.map((preset) => preset.goal),
    ["Answer email", "ship the overlay"],
  );
  // Moves off either end are no-ops, so the end buttons are harmless.
  assert.deepEqual(moveSessionPreset(presets, ids[0], "up"), presets);
  assert.deepEqual(moveSessionPreset(presets, ids[1], "down"), presets);
  assert.deepEqual(moveSessionPreset(presets, "missing", "up"), presets);
  // Reordering never loses or duplicates an entry.
  assert.equal(new Set(moved.map((preset) => preset.id)).size, presets.length);

  assert.equal(removeSessionPreset(presets, ids[0]).length, 1);
  assert.equal(removeSessionPreset(presets, "missing").length, 2);
}

// Round-trip through storage, including the order the user arranged.
{
  const storage = new MemoryStorage();
  const presets = addSessionPreset(
    addSessionPreset([], "Ship the overlay", "deep"),
    "Answer email",
    "recovery",
  );
  writeSessionPresets(presets, storage);
  const restored = readSessionPresets(storage);
  assert.deepEqual(
    restored.map((preset) => [preset.goal, preset.focusMode]),
    [
      ["Ship the overlay", "deep"],
      ["Answer email", "recovery"],
    ],
  );
}

// Corrupt or hostile storage yields an empty list, never a throw into a render.
{
  const storage = new MemoryStorage();
  assert.deepEqual(readSessionPresets(storage), []);
  storage.setItem("snapback.sessionPresets", "{not json");
  assert.deepEqual(readSessionPresets(storage), []);
  storage.setItem("snapback.sessionPresets", '{"goal":"x"}');
  assert.deepEqual(readSessionPresets(storage), []);
  // A partly-valid array keeps the usable rows and drops the rest.
  storage.setItem(
    "snapback.sessionPresets",
    '[{"id":"a","goal":"Real","focusMode":"deep"},{"goal":"   "},null,7]',
  );
  assert.deepEqual(readSessionPresets(storage), [
    { id: "a", goal: "Real", focusMode: "deep" },
  ]);
}

// Absent storage is survivable in both directions.
assert.deepEqual(readSessionPresets(null), []);
writeSessionPresets([], null);

// ---------------------------------------------------------------------------
// Elapsed time. THE RULE: the backend timestamp is the origin, `nowMs` is only the tick.
// ---------------------------------------------------------------------------

{
  const started = "2026-08-10T09:00:00Z";
  const at = (ms: number) => formatElapsed(started, Date.parse(started) + ms);

  assert.equal(at(0), "0m 00s");
  assert.equal(at(45_000), "0m 45s");
  assert.equal(at(90_000), "1m 30s");
  assert.equal(at(59 * 60_000), "59m 00s");
  // Past an hour the seconds stop being useful and the minutes stay two-digit, so the value
  // does not change width every second in the header.
  assert.equal(at(3_600_000), "1h 00m");
  assert.equal(at(3_660_000), "1h 01m");
  assert.equal(at(7_845_000), "2h 10m");

  // A missing or unparseable start renders the same "--" as every other absent timestamp.
  assert.equal(formatElapsed(null, Date.now()), "--");
  assert.equal(formatElapsed("not a date", Date.now()), "--");
  assert.equal(formatElapsed(started, Number.NaN), "--");
  // A browser clock a second behind the backend must not render a negative duration.
  assert.equal(at(-1000), "0m 00s");
}

// ---------------------------------------------------------------------------
// Action gating. THE RULE: no duplicate request, and no enabled button that would no-op.
// ---------------------------------------------------------------------------

assert.equal(canStartSession("Ship it", false, false), true);
// Every reason Start must be dead, one at a time.
assert.equal(canStartSession("", false, false), false, "blank goal cannot start");
assert.equal(canStartSession("Ship it", true, false), false, "pending request cannot restart");
assert.equal(canStartSession("Ship it", false, true), false, "a live session cannot restart");

const active = { status: "ACTIVE" } as never;
const done = { status: "COMPLETED" } as never;
assert.equal(canStopSession(active, false), true);
assert.equal(canStopSession(active, true), false, "pending request cannot re-stop");
assert.equal(canStopSession(done, false), false, "a finished session cannot be stopped again");
assert.equal(canStopSession(null, false), false, "no session, nothing to stop");

// ---------------------------------------------------------------------------
// Goal suggestions (Roadmap 2.15).
// ---------------------------------------------------------------------------

{
  const recent = [
    { goal: "Review PRs", focusMode: "normal" as const },
    { goal: "Debug auth service", focusMode: "deep" as const },
    { goal: "Write documentation", focusMode: "recovery" as const },
  ];
  const presets: SessionPreset[] = [
    { id: "p1", goal: "Ship the overlay", focusMode: "deep" as const },
    { id: "p2", goal: "Review PRs", focusMode: "deep" as const },
  ];

  // Empty query returns pinned presets first, followed by distinct recent goals.
  const allSuggestions = filterGoalSuggestions(recent, presets, "");
  assert.equal(allSuggestions.length, 4);
  assert.deepEqual(allSuggestions[0], {
    goal: "Ship the overlay",
    focusMode: "deep",
    source: "pinned",
  });
  assert.deepEqual(allSuggestions[1], {
    goal: "Review PRs",
    focusMode: "deep",
    source: "pinned",
  });
  assert.deepEqual(allSuggestions[2], {
    goal: "Debug auth service",
    focusMode: "deep",
    source: "recent",
  });
  assert.deepEqual(allSuggestions[3], {
    goal: "Write documentation",
    focusMode: "recovery",
    source: "recent",
  });

  // Query filter matches substring case-insensitively
  const authMatches = filterGoalSuggestions(recent, presets, "auth");
  assert.equal(authMatches.length, 1);
  assert.equal(authMatches[0].goal, "Debug auth service");

  const reviewMatches = filterGoalSuggestions(recent, presets, "review");
  assert.equal(reviewMatches.length, 1);
  assert.equal(reviewMatches[0].source, "pinned"); // Pinned deduplicates against recent

  // Limit bounds result count
  assert.equal(filterGoalSuggestions(recent, presets, "", 2).length, 2);
}

console.log("sessionCockpit.test.ts passed");

