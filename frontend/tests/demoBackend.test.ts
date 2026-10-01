import assert from "node:assert/strict";

import { DemoBackend } from "../demo/backend";

const backend = () => new DemoBackend(Date.now());

type Status = { state: string; privatePauseRemainingMs: number };

// Anything that would touch a real disk rejects. The frontend reads a resolved value
// as success, so resolving here once became "Exported 0 sessions, complete history".
for (const command of [
  "export_support_bundle",
  "export_my_data",
  "export_summary_report",
  "export_training_data",
  "open_data_folder",
  "train_from_export",
  "cancel_training",
  "set_training_repo_path",
]) {
  assert.throws(
    () => backend().handle(command, {}),
    /disabled in the browser demo/,
    `${command} must refuse, not resolve a fake success`,
  );
}

// The read-only probes keep their shapes: their callers already render a refusal
// faithfully (warning tone, verbatim message), and a file dialog that never opened
// is a cancellation the caller no-ops on.
{
  const inspect = backend().handle("inspect_data_import", { path: "x" }) as Record<string, unknown>;
  assert.equal(inspect.acceptable, false);
  assert.match(String(inspect.message), /browser demo/);

  const staged = backend().handle("stage_data_import", { path: "x" }) as Record<string, unknown>;
  assert.equal(staged.ok, false);

  const picked = backend().handle("pick_open_file", {}) as Record<string, unknown>;
  assert.equal(picked.cancelled, true);
}

// 0 means indefinite, matching the native side — not "now plus nothing", which lapses
// before the next status read and made "Pause until I resume" a no-op.
{
  const demo = backend();
  const paused = demo.handle("pause_recording_privately", { minutes: 0 }) as Status;
  assert.equal(paused.state, "pausedPrivate");
  const again = demo.handle("get_recording_status", {}) as Status;
  assert.equal(again.state, "pausedPrivate");
  assert.equal(again.privatePauseRemainingMs, 0);

  const resumed = demo.handle("resume_recording", {}) as Status;
  assert.notEqual(resumed.state, "pausedPrivate");
}

// A timed pause still pauses, with a countdown, and still ends on resume.
{
  const demo = backend();
  const paused = demo.handle("pause_recording_privately", { minutes: 30 }) as Status;
  assert.equal(paused.state, "pausedPrivate");
  assert.ok(paused.privatePauseRemainingMs > 0);
}

console.log("demoBackend.test.ts passed");

// Newly opened demo sessions have no generated observations until input is simulated.
{
  const demo = backend();
  const session = demo.handle("start_session", { goal: "No signal", focusMode: "normal" }) as Record<string, unknown>;
  demo.handle("stop_session", { sessionId: session.sessionId });
  const recap = demo.handle("get_session_recap", { sessionId: session.sessionId }) as Record<string, unknown>;
  assert.equal(recap.sampleCount, 0);
  assert.equal(demo.handle("get_session_auto_label", { sessionId: session.sessionId }), null);
  demo.handle("stop_session", { sessionId: session.sessionId });
  assert.equal(demo.handle("get_session_auto_label", { sessionId: session.sessionId }), null);
}

{
  const demo = backend();
  const session = demo.handle("start_session", { goal: "Feedback", focusMode: "normal" }) as Record<string, unknown>;
  const request = { sessionId: session.sessionId, label: "PRODUCTIVE", source: "manual" };
  demo.handle("submit_label", { request });
  demo.handle("stop_session", { sessionId: session.sessionId });
  assert.throws(() => demo.handle("submit_label", { request }), /Start a session/);
  demo.handle("submit_label", { request: { ...request, source: "survey", notes: "confirmed automatic label" } });
  assert.equal(demo.handle("get_session_rating", { sessionId: session.sessionId }), "PRODUCTIVE");
  const logs = () => (demo.handle("get_diagnostics", {}) as { recentLogs: string[] }).recentLogs.join("\n");
  assert.match(logs(), /2 feedback submissions retained/);
  assert.throws(() => demo.handle("submit_label", { request: { ...request, source: "survey", label: "INVENTED" } }), /Unknown focus label/);
  demo.handle("delete_session", { sessionId: session.sessionId });
  assert.match(logs(), /0 feedback submissions retained/);
  assert.throws(() => demo.handle("submit_label", { request: { ...request, source: "survey" } }), /No such session/);
}

// A recording pause freezes demo observations and attendance, while elapsed time continues.
{
  let now = Date.now();
  const demo = new DemoBackend(now, () => now);
  demo.handle("delete_all_activity_data", {});
  const session = demo.handle("start_session", { goal: "Pause semantics" }) as { sessionId: string };
  now += 18_000;
  assert.ok(demo.tick());
  demo.handle("pause_recording_privately", { minutes: 0 });
  now += 40_000;
  assert.equal(demo.tick(), null);
  const recap = () => demo.handle("get_session_recap", { sessionId: session.sessionId }) as { activeSecs: number; durationSecs: number; sampleCount: number };
  assert.equal(recap().activeSecs, 18);
  assert.equal(recap().sampleCount, 1);
  demo.handle("resume_recording", {});
  now += 20_000;
  const stopped = demo.handle("stop_session", { sessionId: session.sessionId }) as { endedAtMs: number };
  assert.equal(recap().activeSecs, 38);
  assert.equal(recap().durationSecs, 78);
  now += 10_000;
  assert.equal((demo.handle("stop_session", { sessionId: session.sessionId }) as { endedAtMs: number }).endedAtMs, stopped.endedAtMs);
}
{
  let now = Date.now();
  const demo = new DemoBackend(now, () => now);
  demo.handle("delete_all_activity_data", {});
  const session = demo.handle("start_session", { goal: "Timed pause" }) as { sessionId: string };
  demo.handle("pause_recording_privately", { minutes: 1 });
  now += 75_000;
  assert.ok(demo.tick());
  const recap = demo.handle("get_session_recap", { sessionId: session.sessionId }) as { activeSecs: number };
  assert.equal(recap.activeSecs, 15);
}

// Demo phase actions follow the native timer, with controllable time instead of fixed snapshots.
{
  let now = Date.now();
  const demo = new DemoBackend(now, () => now);
  demo.handle("delete_all_activity_data", {});
  assert.throws(() => demo.handle("start_pomodoro", {}), /Start a session/);
  const session = demo.handle("start_session", { goal: "Timer" }) as { sessionId: string };
  type Timer = { phase: string; remainingMs: number; running: boolean; paused: boolean;
    awaitingAcknowledgement: boolean; completedWorkIntervals: number };
  const timer = (command = "get_pomodoro_status") => demo.handle(command, {}) as Timer;
  demo.handle("set_pomodoro_config", { config: { workMs: 60_000, shortBreakMs: 30_000,
    longBreakMs: 90_000, intervalsBeforeLongBreak: 2, autoStartNextPhase: true } });
  assert.equal(timer("start_pomodoro").remainingMs, 60_000);
  now += 20_000;
  assert.equal(timer("pause_pomodoro").remainingMs, 40_000);
  now += 100_000;
  assert.equal(timer().remainingMs, 40_000);
  timer("resume_pomodoro");
  now += 10_000;
  assert.equal(timer().remainingMs, 30_000);
  const skipped = timer("skip_pomodoro_phase");
  assert.equal(skipped.phase, "shortBreak");
  assert.equal(skipped.completedWorkIntervals, 0);
  timer("skip_pomodoro_phase");
  now += 65_000;
  assert.equal(timer().phase, "shortBreak");
  assert.equal(timer().remainingMs, 25_000);
  assert.equal(timer().completedWorkIntervals, 1);
  now += 85_000;
  assert.equal(timer().phase, "longBreak");
  assert.equal(timer().completedWorkIntervals, 2);
  assert.equal(timer().remainingMs, 90_000);
  now += 10_000;
  assert.equal(timer("restart_pomodoro_phase").remainingMs, 90_000);
  demo.handle("set_pomodoro_config", { config: { autoStartNextPhase: false } });
  now += 90_000;
  assert.equal(timer().phase, "work");
  assert.equal(timer().awaitingAcknowledgement, true);
  now += 120_000;
  assert.equal(timer().remainingMs, 0);
  assert.equal(timer("acknowledge_pomodoro_phase").remainingMs, 60_000);
  now += 5000;
  assert.equal(timer("acknowledge_pomodoro_phase").remainingMs, 55_000);
  demo.handle("stop_session", { sessionId: session.sessionId });
  assert.equal(timer().running, false);
  assert.equal(timer().completedWorkIntervals, 0);
  assert.throws(() => demo.handle("set_pomodoro_config", { config: { workMs: 0 } }), /positive integers/);
}

// Skip while awaiting starts the pending phase; intervalsBeforeLongBreak 0 never schedules a long break.
{
  let now = Date.now();
  const demo = new DemoBackend(now, () => now);
  demo.handle("delete_all_activity_data", {});
  demo.handle("start_session", { goal: "Awaiting" });
  demo.handle("set_pomodoro_config", {
    config: {
      workMs: 1000,
      shortBreakMs: 1000,
      longBreakMs: 1000,
      intervalsBeforeLongBreak: 0,
      autoStartNextPhase: false,
    },
  });
  demo.handle("start_pomodoro", {});
  now += 1000;
  type Timer = { phase: string; remainingMs: number; awaitingAcknowledgement: boolean };
  const waiting = demo.handle("get_pomodoro_status", {}) as Timer;
  assert.equal(waiting.phase, "shortBreak");
  assert.equal(waiting.awaitingAcknowledgement, true);
  assert.equal((demo.handle("skip_pomodoro_phase", {}) as Timer).phase, "shortBreak");
  assert.equal((demo.handle("get_pomodoro_status", {}) as Timer).awaitingAcknowledgement, false);
  now += 1000;
  demo.handle("get_pomodoro_status", {});
  assert.equal((demo.handle("restart_pomodoro_phase", {}) as Timer).phase, "shortBreak");
  assert.throws(
    () => demo.handle("set_pomodoro_config", { config: { intervalsBeforeLongBreak: -1 } }),
    /nonnegative/,
  );
}

// Review "Session time" is completed wall-clock duration, not prediction-count * 120.
// A short busy session must not read as half an hour, and all-time never invents a plan.
{
  let now = Date.now();
  const demo = new DemoBackend(now, () => now);
  demo.handle("delete_all_activity_data", {});
  demo.handle("set_attended_targets", { dailyMins: 240, weeklyMins: 1200 });
  const session = demo.handle("start_session", { goal: "Short session" }) as { sessionId: string };
  for (let i = 0; i < 15; i += 1) {
    now += 5000;
    assert.ok(demo.tick());
  }
  demo.handle("stop_session", { sessionId: session.sessionId });
  const day = demo.handle("get_summary_report", { window: "day" }) as {
    focusSeconds: number; plannedMins: number; completedSessionCount: number; sampleCount: number;
  };
  assert.equal(day.completedSessionCount, 1);
  assert.equal(day.focusSeconds, 75);
  assert.ok(day.sampleCount >= 15);
  assert.notEqual(day.focusSeconds, day.sampleCount * 120);
  assert.equal(day.plannedMins, 240);
  const all = demo.handle("get_summary_report", { window: "all" }) as {
    focusSeconds: number; plannedMins: number;
  };
  assert.equal(all.focusSeconds, 75);
  assert.equal(all.plannedMins, 0);
}

// Fragments (under 60s, no predictions) stay listable but do not inflate Review totals.
{
  let now = Date.now();
  const demo = new DemoBackend(now, () => now);
  demo.handle("delete_all_activity_data", {});
  const session = demo.handle("start_session", { goal: "Oops" }) as { sessionId: string };
  now += 15_000;
  demo.handle("stop_session", { sessionId: session.sessionId });
  const day = demo.handle("get_summary_report", { window: "day" }) as {
    focusSeconds: number;
    completedSessionCount: number;
  };
  assert.equal(day.completedSessionCount, 0);
  assert.equal(day.focusSeconds, 0);
  const history = demo.handle("get_session_history", { window: "day" }) as unknown[];
  assert.equal(history.length, 1);
}

// Demo settings survive a reload (new DemoBackend after localStorage write); sessions do not.
{
  const store = new Map<string, string>();
  const memory = {
    getItem: (key: string) => store.get(key) ?? null,
    setItem: (key: string, value: string) => {
      store.set(key, value);
    },
    removeItem: (key: string) => {
      store.delete(key);
    },
  };
  const previous = globalThis.localStorage;
  Object.defineProperty(globalThis, "localStorage", { value: memory, configurable: true });
  try {
    const now = Date.now();
    const first = new DemoBackend(now, () => now);
    first.handle("set_focus_mode", { mode: "recovery" });
    first.handle("set_idle_threshold", { seconds: 120 });
    first.handle("set_attended_targets", { dailyMins: 90, weeklyMins: 450 });
    first.handle("upsert_app_rule", {
      request: { pattern: "Slack", ruleType: "block", note: "chat" },
    });
    first.handle("set_privacy_exclusions", { excludedApps: ["Signal"] });
    first.handle("set_alert_delivery", {
      alerts: { snapback: ["native"], hyperfocus: ["overlay"], pomodoro: ["inApp"] },
    });
    const session = first.handle("start_session", { goal: "Ephemeral" }) as { sessionId: string };
    assert.ok(session.sessionId);

    const second = new DemoBackend(now + 1, () => now + 1);
    const settings = second.handle("get_settings", {}) as {
      defaultFocusMode: string;
      idleThresholdSecs: number;
    };
    assert.equal(settings.defaultFocusMode, "recovery");
    assert.equal(settings.idleThresholdSecs, 120);
    const progress = second.handle("get_attended_progress", {}) as {
      dailyTargetMins: number;
      weeklyTargetMins: number;
    };
    assert.equal(progress.dailyTargetMins, 90);
    assert.equal(progress.weeklyTargetMins, 450);
    const rules = second.handle("get_app_rules", {}) as { pattern: string }[];
    assert.ok(rules.some((rule) => rule.pattern === "Slack"));
    const privacy = second.handle("get_privacy_settings", {}) as { excludedApps: string[] };
    assert.deepEqual(privacy.excludedApps, ["Signal"]);
    // Sessions stay ephemeral across demo reloads.
    assert.equal(second.handle("get_active_session", {}), null);
  } finally {
    Object.defineProperty(globalThis, "localStorage", { value: previous, configurable: true });
  }
}
