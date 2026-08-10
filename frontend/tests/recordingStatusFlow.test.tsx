import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Roadmap 2.10. The status is decided in the backend; this proves the card reports it rather
// than re-deriving it, and that the pause controls drive the one command.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    settings: Record<string, unknown>;
    recording: Record<string, unknown>;
    pausedWith: Record<string, unknown> | null;
    resumed: boolean;
  } = {
    health: {},
    settings: {},
    recording: { state: "noSession", private_pause_remaining_ms: 0 },
    pausedWith: null,
    resumed: false,
  };

  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
      case "get_settings":
        return state.settings;
      case "get_recording_status":
        return state.recording;
      case "pause_recording_privately":
        state.pausedWith = args ?? {};
        state.recording = {
          state: "pausedPrivate",
          private_pause_remaining_ms: Number(args?.minutes ?? 0) * 60 * 1000,
        };
        return state.recording;
      case "resume_recording":
        state.resumed = true;
        state.recording = { state: "noSession", private_pause_remaining_ms: 0 };
        return state.recording;
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

  const listen = vi.fn(async () => () => {});
  return { state, invoke, listen };
});

vi.mock("../src/bridge", () => ({ invoke: boundary.invoke, listen: boundary.listen }));

import App from "../src/App";

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
  boundary.state.pausedWith = null;
  boundary.state.resumed = false;
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
    expect(await within(card()).findByText("No session")).toBeInTheDocument();
  });

  it("shows an unknown state as blocked rather than as recording", async () => {
    // If the two ends ever disagree, the claim that cannot mislead is "nothing is captured".
    boundary.state.recording = { state: "somethingNew", private_pause_remaining_ms: 0 };
    render(<App />);
    await screen.findByRole("heading", { name: "Recording status" });
    expect(await within(card()).findByText("Blocked")).toBeInTheDocument();
  });
});
