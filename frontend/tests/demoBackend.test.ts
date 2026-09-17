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
