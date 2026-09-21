import { act, cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Roadmap 2.10. The status is decided in the backend; this proves the card reports it rather
// than re-deriving it, and that the pause controls drive the one command.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    settings: Record<string, unknown>;
    recording: Record<string, unknown>;
    privacy: Record<string, unknown>;
    pausedWith: Record<string, unknown> | null;
    resumed: boolean;
    // When set, the next get_recording_status waits on this instead of answering; the test
    // decides when (and whether) it lands.
    holdStatus: Promise<unknown> | null;
    statusFails: boolean;
  } = {
    health: {},
    settings: {},
    recording: { state: "noSession", private_pause_remaining_ms: 0 },
    privacy: { private_mode: false, excluded_apps: [], local_only: true },
    pausedWith: null,
    resumed: false,
    holdStatus: null,
    statusFails: false,
  };
  const listeners: Record<string, Array<(event: { payload: unknown }) => void>> = {};

  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
      case "get_settings":
        return state.settings;
      case "get_recording_status": {
        if (state.statusFails) throw new Error("bridge down");
        const held = state.holdStatus;
        if (held) {
          state.holdStatus = null;
          return held;
        }
        return state.recording;
      }
      case "pause_recording_privately":
        state.pausedWith = args ?? {};
        state.privacy = { ...state.privacy, private_mode: true };
        state.recording = {
          state: "pausedPrivate",
          private_pause_remaining_ms: Number(args?.minutes ?? 0) * 60 * 1000,
        };
        return state.recording;
      case "resume_recording":
        state.resumed = true;
        state.privacy = { ...state.privacy, private_mode: false };
        state.recording = { state: "noSession", private_pause_remaining_ms: 0 };
        return state.recording;
      case "get_privacy_settings":
        return state.privacy;
      case "set_private_mode":
        state.privacy = { ...state.privacy, private_mode: Boolean(args?.enabled) };
        state.recording = {
          state: args?.enabled ? "pausedPrivate" : "recording",
          private_pause_remaining_ms: 0,
        };
        return state.privacy;
      case "get_attended_progress":
        return {
          daily_target_mins: 0,
          daily_actual_mins: 0,
          weekly_target_mins: 0,
          weekly_actual_mins: 0,
        };
      case "get_pomodoro_status":
        return { running: false, phase: "work", completed_work_intervals: 0, remaining_ms: 0 };
      case "get_prediction_history":
      case "get_app_rules":
      case "get_context_timeline":
      case "get_session_history":
        return [];
      case "get_training_deploy_status":
        return {};
      default:
        return null;
    }
  });

  const listen = vi.fn(async (event: string, handler: (e: { payload: unknown }) => void) => {
    (listeners[event] ??= []).push(handler);
    return () => {};
  });
  const emit = (event: string, payload: unknown) => {
    for (const handler of listeners[event] ?? []) handler({ payload });
  };
  return { state, invoke, listen, emit };
});

vi.mock("../src/bridge", () => ({ invoke: boundary.invoke, listen: boundary.listen }));

import App from "../src/App";
import { DEADLINE_SLACK_MS } from "../src/useRecordingStatus";
import { renderApp } from "./renderApp";

const healthy = (): Record<string, unknown> => ({
  status: "online",
  capture_running: true,
  capture_failed: false,
  capture_events_dropped: 0,
  permissions: {
    capture_available: true,
    capture_probe_confirmed: true,
    active_window_available: true,
    message: "",
    setup_steps: [],
  },
  classifier: { backend: "heuristic", onnx_runtime_enabled: false, model_path: null },
});

const card = () =>
  screen.getByRole("heading", { name: "Recording status" }).closest("section") as HTMLElement;

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthy();
  boundary.state.settings = { default_focus_mode: "normal" };
  boundary.state.recording = { state: "noSession", private_pause_remaining_ms: 0 };
  boundary.state.privacy = { private_mode: false, excluded_apps: [], local_only: true };
  boundary.state.pausedWith = null;
  boundary.state.resumed = false;
  boundary.state.holdStatus = null;
  boundary.state.statusFails = false;
});

afterEach(() => {
  cleanup();
});

describe("Recording status", () => {
  it("reports the state the backend decided, not one it derived", async () => {
    boundary.state.recording = { state: "recording", private_pause_remaining_ms: 0 };
    render(<App />);
    await screen.findByRole("heading", { name: "Recording status" });
    expect(await within(card()).findByText("Recording")).toBeInTheDocument();
  });

  it("distinguishes idle from private, which both stop capture", async () => {
    boundary.state.recording = { state: "pausedIdle", private_pause_remaining_ms: 0 };
    render(<App />);
    await screen.findByRole("heading", { name: "Recording status" });
    expect(await within(card()).findByText("Paused for idle")).toBeInTheDocument();
    // Idle is not a privacy pause, so it is not offering to resume from one.
    expect(
      within(card()).queryByRole("button", { name: "Resume recording" }),
    ).not.toBeInTheDocument();
  });

  it("pauses for a fixed stretch and shows the time left", async () => {
    boundary.state.recording = { state: "recording", private_pause_remaining_ms: 0 };
    render(<App />);
    await screen.findByRole("heading", { name: "Recording status" });

    fireEvent.click(within(card()).getByRole("button", { name: "Pause 30m" }));
    await waitFor(() => expect(boundary.state.pausedWith).not.toBeNull());
    expect(boundary.state.pausedWith).toEqual({ minutes: 30 });

    expect(await within(card()).findByText("Paused privately")).toBeInTheDocument();
    expect(await within(card()).findByText(/30m left/)).toBeInTheDocument();
    expect(await within(card()).findByText(/resumes on its own/)).toBeInTheDocument();
  });

  it("pauses indefinitely without promising a resume time", async () => {
    boundary.state.recording = { state: "recording", private_pause_remaining_ms: 0 };
    render(<App />);
    await screen.findByRole("heading", { name: "Recording status" });

    fireEvent.click(within(card()).getByRole("button", { name: "Pause until I resume" }));
    await waitFor(() => expect(boundary.state.pausedWith).not.toBeNull());
    expect(boundary.state.pausedWith).toEqual({ minutes: 0 });
    expect(await within(card()).findByText("Paused privately")).toBeInTheDocument();
    expect(within(card()).queryByText(/left/)).not.toBeInTheDocument();
  });

  it("resumes through the same command the tray would use", async () => {
    boundary.state.recording = { state: "pausedPrivate", private_pause_remaining_ms: 0 };
    render(<App />);
    await screen.findByRole("heading", { name: "Recording status" });

    fireEvent.click(within(card()).getByRole("button", { name: "Resume recording" }));
    await waitFor(() => expect(boundary.state.resumed).toBe(true));
    expect(await within(card()).findByText("Not recording")).toBeInTheDocument();
  });

  it("shows an unknown state as blocked rather than as recording", async () => {
    // If the two ends ever disagree, the claim that cannot mislead is "nothing is captured".
    boundary.state.recording = { state: "somethingNew", private_pause_remaining_ms: 0 };
    render(<App />);
    await screen.findByRole("heading", { name: "Recording status" });
    expect(await within(card()).findByText("Blocked")).toBeInTheDocument();
  });
});

// Slice 3 of the Astra review (Roadmap 2.10 / 2.16 / 14.4): the header, Settings, and the
// native state must agree after every way the answer can change -- not only after the clicks
// this side makes.
describe("Recording status stays coherent with native state", () => {
  const recordingCalls = () =>
    boundary.invoke.mock.calls.filter(([cmd]) => cmd === "get_recording_status").length;

  it("re-asks when the engine reports an idle transition", async () => {
    boundary.state.recording = { state: "recording", private_pause_remaining_ms: 0 };
    render(<App />);
    expect(await within(card()).findByText("Recording")).toBeInTheDocument();

    // The user walks away. The engine says so; nothing on this side clicked anything.
    boundary.state.recording = { state: "pausedIdle", private_pause_remaining_ms: 0 };
    act(() => boundary.emit("idle", { idle: true }));
    expect(await within(card()).findByText("Paused for idle")).toBeInTheDocument();

    boundary.state.recording = { state: "recording", private_pause_remaining_ms: 0 };
    act(() => boundary.emit("idle", { idle: false }));
    expect(await within(card()).findByText("Recording")).toBeInTheDocument();
  });

  it("applies a tray-originated change without a command from this side", async () => {
    boundary.state.recording = { state: "recording", private_pause_remaining_ms: 0 };
    render(<App />);
    expect(await within(card()).findByText("Recording")).toBeInTheDocument();
    const before = recordingCalls();

    // The tray paused recording. The native side announces the new answer directly.
    act(() =>
      boundary.emit("recording-status", { state: "pausedPrivate", private_pause_remaining_ms: 0 }),
    );
    expect(await within(card()).findByText("Paused privately")).toBeInTheDocument();
    // Applied, not re-fetched: the event carried the answer.
    expect(recordingCalls()).toBe(before);
  });

  it("asks again when a timed pause reaches its deadline", async () => {
    vi.useFakeTimers();
    try {
      const pauseMs = 5 * 60 * 1000;
      boundary.state.recording = { state: "pausedPrivate", private_pause_remaining_ms: pauseMs };
      render(<App />);
      await act(async () => {
        await vi.advanceTimersByTimeAsync(0);
      });
      expect(within(card()).getByText("Paused privately")).toBeInTheDocument();

      // Natively the pause lapses on the read that finds the deadline passed.
      boundary.state.recording = { state: "recording", private_pause_remaining_ms: 0 };
      const asked = recordingCalls();
      await act(async () => {
        await vi.advanceTimersByTimeAsync(pauseMs - 1000);
      });
      // Not yet: nothing polls a running countdown.
      expect(recordingCalls()).toBe(asked);
      expect(within(card()).getByText("Paused privately")).toBeInTheDocument();

      await act(async () => {
        await vi.advanceTimersByTimeAsync(1000 + DEADLINE_SLACK_MS);
      });
      expect(recordingCalls()).toBe(asked + 1);
      expect(within(card()).getByText("Recording")).toBeInTheDocument();
    } finally {
      vi.useRealTimers();
    }
  });

  it("never lets an older answer overwrite a newer one", async () => {
    boundary.state.recording = { state: "recording", private_pause_remaining_ms: 0 };
    render(<App />);
    expect(await within(card()).findByText("Recording")).toBeInTheDocument();

    // A refresh goes out and stalls...
    let release: (value: unknown) => void = () => {};
    boundary.state.holdStatus = new Promise((resolve) => {
      release = resolve;
    });
    act(() => boundary.emit("idle", { idle: false }));
    await waitFor(() => expect(boundary.state.holdStatus).toBeNull());

    // ...the user pauses meanwhile, and that answer shows...
    fireEvent.click(within(card()).getByRole("button", { name: "Pause until I resume" }));
    expect(await within(card()).findByText("Paused privately")).toBeInTheDocument();

    // ...then the stale refresh lands with the pre-pause answer. It must be ignored.
    await act(async () => {
      release({ state: "recording", private_pause_remaining_ms: 0 });
      await Promise.resolve();
    });
    expect(within(card()).getByText("Paused privately")).toBeInTheDocument();
    expect(within(card()).queryByText("Recording")).not.toBeInTheDocument();
  });

  it("says so when it cannot confirm the state, and stops saying so once it can", async () => {
    boundary.state.recording = { state: "recording", private_pause_remaining_ms: 0 };
    render(<App />);
    expect(await within(card()).findByText("Recording")).toBeInTheDocument();
    expect(within(card()).queryByText(/Could not confirm/)).not.toBeInTheDocument();

    boundary.state.statusFails = true;
    act(() => boundary.emit("idle", { idle: true }));
    expect(await within(card()).findByText(/Could not confirm/)).toBeInTheDocument();
    // The last known state is still shown rather than replaced with a guess.
    expect(within(card()).getByText("Recording")).toBeInTheDocument();

    boundary.state.statusFails = false;
    boundary.state.recording = { state: "pausedIdle", private_pause_remaining_ms: 0 };
    act(() => boundary.emit("idle", { idle: true }));
    expect(await within(card()).findByText("Paused for idle")).toBeInTheDocument();
    expect(within(card()).queryByText(/Could not confirm/)).not.toBeInTheDocument();
  });

  it("keeps the Settings toggle and the header in agreement in both directions", async () => {
    boundary.state.recording = { state: "recording", private_pause_remaining_ms: 0 };
    renderApp("settings", "privacy");
    expect(await within(card()).findByText("Recording")).toBeInTheDocument();
    const toggle = () => screen.getByRole("checkbox", { name: "Private mode" }) as HTMLInputElement;
    await waitFor(() => expect(toggle().disabled).toBe(false));
    expect(toggle().checked).toBe(false);

    // Settings -> header.
    fireEvent.click(toggle());
    expect(await within(card()).findByText("Paused privately")).toBeInTheDocument();
    await waitFor(() => expect(toggle().checked).toBe(true));

    // Header -> Settings.
    fireEvent.click(within(card()).getByRole("button", { name: "Resume recording" }));
    expect(await within(card()).findByText("Not recording")).toBeInTheDocument();
    await waitFor(() => expect(toggle().checked).toBe(false));

    // Tray -> both, with no command from this side.
    boundary.state.privacy = { ...boundary.state.privacy, private_mode: true };
    act(() =>
      boundary.emit("recording-status", { state: "pausedPrivate", private_pause_remaining_ms: 0 }),
    );
    expect(await within(card()).findByText("Paused privately")).toBeInTheDocument();
    await waitFor(() => expect(toggle().checked).toBe(true));
  });
});
