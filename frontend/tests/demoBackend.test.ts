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
