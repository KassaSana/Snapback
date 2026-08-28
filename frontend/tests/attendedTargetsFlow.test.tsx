import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Roadmap 2.19. Drives the real card + hook + api against a mocked native boundary.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    settings: Record<string, unknown>;
    progress: Record<string, unknown>;
    savedTargets: Record<string, unknown> | null;
  } = {
    health: {},
    settings: {},
    progress: {
      daily_target_mins: 0,
      daily_actual_mins: 0,
      weekly_target_mins: 0,
      weekly_actual_mins: 0,
    },
    savedTargets: null,
  };

  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
      case "get_settings":
        return state.settings;
      case "start_session":
        return {
          session_id: "sess-42",
          goal: String(args?.goal ?? ""),
          status: "ACTIVE",
          focus_mode: String(args?.focusMode ?? "normal"),
          started_at_ms: Date.parse("2026-07-11T00:00:00Z"),
          ended_at_ms: null,
        };
      case "get_attended_progress":
        return state.progress;
      case "set_attended_targets":
        state.savedTargets = args ?? {};
        state.progress = {
          ...state.progress,
          daily_target_mins: Number(args?.dailyMins ?? 0),
          weekly_target_mins: Number(args?.weeklyMins ?? 0),
        };
        return state.progress;
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

const healthyCaptureRunning = (): Record<string, unknown> => ({
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

const startSession = async () => {
  render(<App />);
  await screen.findByRole("heading", { name: "Session Control" });
  fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
    target: { value: "Write tests" },
  });
  fireEvent.click(screen.getByRole("button", { name: "Start session" }));
  await screen.findByText("running");
};

const targetsCard = () =>
  screen.getByRole("heading", { name: "Attended time" }).closest("section") as HTMLElement;

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.settings = { default_focus_mode: "normal" };
  boundary.state.progress = {
    daily_target_mins: 0,
    daily_actual_mins: 0,
    weekly_target_mins: 0,
    weekly_actual_mins: 0,
  };
  boundary.state.savedTargets = null;
});

afterEach(() => {
  cleanup();
});

describe("Attended-time targets", () => {
  it("shows attendance with no target set, and offers to set one", async () => {
    boundary.state.progress = {
      daily_target_mins: 0,
      daily_actual_mins: 95,
      weekly_target_mins: 0,
      weekly_actual_mins: 320,
    };
    await startSession();
    const card = await screen.findByRole("heading", { name: "Attended time" });
    const section = card.closest("section") as HTMLElement;

    expect(await within(section).findByText(/1h 35m/)).toBeInTheDocument();
    expect(within(section).getByRole("button", { name: "Set a target" })).toBeInTheDocument();
  });

  it("reports progress against a target as a plain ratio", async () => {
    boundary.state.progress = {
      daily_target_mins: 240,
      daily_actual_mins: 120,
      weekly_target_mins: 1200,
      weekly_actual_mins: 300,
    };
    await startSession();
    await screen.findByRole("heading", { name: "Attended time" });
    const section = targetsCard();

    expect(await within(section).findByText(/of 4h 0m planned/)).toBeInTheDocument();
    expect(within(section).getByRole("button", { name: "Edit targets" })).toBeInTheDocument();
  });

  it("saves a target and renders what the backend accepted", async () => {
    await startSession();
    await screen.findByRole("heading", { name: "Attended time" });
    const section = targetsCard();
    fireEvent.click(within(section).getByRole("button", { name: "Set a target" }));
    fireEvent.change(within(section).getByLabelText(/Daily target/), {
      target: { value: "180" },
    });
    fireEvent.change(within(section).getByLabelText(/Weekly target/), {
      target: { value: "900" },
    });
    fireEvent.click(within(section).getByRole("button", { name: "Save targets" }));

    await waitFor(() => expect(boundary.state.savedTargets).not.toBeNull());
    expect(boundary.state.savedTargets).toEqual({ dailyMins: 180, weeklyMins: 900 });
    expect(await within(section).findByText(/of 3h 0m planned/)).toBeInTheDocument();
  });

  it("turns a target off by setting it to zero", async () => {
    boundary.state.progress = {
      daily_target_mins: 240,
      daily_actual_mins: 0,
      weekly_target_mins: 0,
      weekly_actual_mins: 0,
    };
    await startSession();
    await screen.findByRole("heading", { name: "Attended time" });
    const section = targetsCard();

    fireEvent.click(within(section).getByRole("button", { name: "Edit targets" }));
    fireEvent.change(within(section).getByLabelText(/Daily target/), { target: { value: "0" } });
    fireEvent.click(within(section).getByRole("button", { name: "Save targets" }));

    await waitFor(() => expect(boundary.state.savedTargets).not.toBeNull());
    expect(boundary.state.savedTargets).toEqual({ dailyMins: 0, weeklyMins: 0 });
    expect(within(section).queryByText(/planned/)).not.toBeInTheDocument();
  });
});
