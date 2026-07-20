import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Mock the Tauri boundary so the real api.ts + usePomodoro run end to end.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    settings: Record<string, unknown>;
    pomodoro: Record<string, unknown>;
  } = {
    health: {},
    settings: {},
    pomodoro: { running: false, phase: "work", completed_work_intervals: 0, remaining_ms: 0 },
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
          started_at: "2026-07-11T00:00:00Z",
          ended_at: null,
        };
      case "get_pomodoro_status":
        return state.pomodoro;
      case "start_pomodoro":
        state.pomodoro = {
          running: true,
          phase: "work",
          completed_work_intervals: 0,
          remaining_ms: 25 * 60 * 1000,
        };
        return state.pomodoro;
      case "stop_pomodoro":
        state.pomodoro = {
          running: false,
          phase: "work",
          completed_work_intervals: 0,
          remaining_ms: 0,
        };
        return state.pomodoro;
      case "get_prediction_history":
      case "get_app_rules":
      case "get_context_timeline":
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

vi.mock("@tauri-apps/api/core", () => ({ invoke: boundary.invoke }));
vi.mock("@tauri-apps/api/event", () => ({ listen: boundary.listen }));

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

const pomodoroCard = () =>
  screen.getByRole("heading", { name: "Pomodoro" }).closest("section") as HTMLElement;

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.settings = { default_focus_mode: "normal" };
  boundary.state.pomodoro = {
    running: false,
    phase: "work",
    completed_work_intervals: 0,
    remaining_ms: 0,
  };
});

afterEach(() => {
  cleanup();
});

describe("Pomodoro card", () => {
  it("disables the timer until a session is active", async () => {
    render(<App />);
    const card = pomodoroCard();

    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_pomodoro_status"));
    expect(within(card).getByText(/Start a focus session/i)).toBeInTheDocument();
    expect(within(card).queryByRole("button", { name: "Start Pomodoro" })).not.toBeInTheDocument();
  });

  it("starts and stops the timer once a session is active", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });

    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByText("active");

    const card = pomodoroCard();
    fireEvent.click(within(card).getByRole("button", { name: "Start Pomodoro" }));

    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("start_pomodoro"));
    expect(await within(card).findByText("25:00")).toBeInTheDocument();
    expect(within(card).getByRole("button", { name: "Stop Pomodoro" })).toBeInTheDocument();

    fireEvent.click(within(card).getByRole("button", { name: "Stop Pomodoro" }));
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("stop_pomodoro"));
    expect(await within(card).findByText("--:--")).toBeInTheDocument();
  });
});
