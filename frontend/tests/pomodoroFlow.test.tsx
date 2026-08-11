import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Mock the native boundary so the real api.ts + usePomodoro run end to end.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    settings: Record<string, unknown>;
    settingsThrows: boolean;
    pomodoro: Record<string, unknown>;
  } = {
    health: {},
    settings: {},
    settingsThrows: false,
    pomodoro: { running: false, phase: "work", completed_work_intervals: 0, remaining_ms: 0 },
  };

  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
      case "get_settings":
        if (state.settingsThrows) throw new Error("settings unavailable");
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
      case "set_pomodoro_config":
        state.settings = { ...state.settings, pomodoro: args?.config };
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
      case "pause_pomodoro":
        state.pomodoro = { ...state.pomodoro, paused: true };
        return state.pomodoro;
      case "resume_pomodoro":
        state.pomodoro = { ...state.pomodoro, paused: false };
        return state.pomodoro;
      case "skip_pomodoro_phase":
        state.pomodoro = {
          ...state.pomodoro,
          phase: "shortBreak",
          remaining_ms: 5 * 60 * 1000,
        };
        return state.pomodoro;
      case "restart_pomodoro_phase":
        state.pomodoro = { ...state.pomodoro, remaining_ms: 25 * 60 * 1000 };
        return state.pomodoro;
      case "acknowledge_pomodoro_phase":
        state.pomodoro = {
          ...state.pomodoro,
          awaiting_acknowledgement: false,
          phase: "shortBreak",
          remaining_ms: 5 * 60 * 1000,
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

const pomodoroCard = () =>
  screen.getByRole("heading", { name: "Pomodoro" }).closest("section") as HTMLElement;

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.settings = { default_focus_mode: "normal" };
  boundary.state.settingsThrows = false;
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
    await screen.findByText("running");  // Roadmap 7.23: running/paused, not "active"

    const card = pomodoroCard();
    fireEvent.click(within(card).getByRole("button", { name: "Start Pomodoro" }));

    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("start_pomodoro"));
    expect(await within(card).findByText("25:00")).toBeInTheDocument();
    expect(within(card).getByRole("button", { name: "Stop Pomodoro" })).toBeInTheDocument();

    fireEvent.click(within(card).getByRole("button", { name: "Stop Pomodoro" }));
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("stop_pomodoro"));
    expect(await within(card).findByText("--:--")).toBeInTheDocument();
  });

  it("still renders timer status when settings hydration fails", async () => {
    boundary.state.settingsThrows = true;
    boundary.state.pomodoro = {
      running: true,
      phase: "work",
      completed_work_intervals: 0,
      remaining_ms: 12 * 60 * 1000,
    };

    render(<App />);
    const card = pomodoroCard();

    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_pomodoro_status"));
    expect(await within(card).findByText("12:00")).toBeInTheDocument();
  });

  // Roadmap 2.13. Drives the real card + usePomodoro + api.ts against the mocked boundary,
  // so a control that is wired to the wrong command fails here rather than in the app.
  const startSessionAndTimer = async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });
    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByText("running");
    const card = pomodoroCard();
    fireEvent.click(within(card).getByRole("button", { name: "Start Pomodoro" }));
    await within(card).findByText("25:00");
    return card;
  };

  it("pauses and resumes, swapping the control rather than showing both", async () => {
    const card = await startSessionAndTimer();

    fireEvent.click(within(card).getByRole("button", { name: "Pause" }));
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("pause_pomodoro"));
    expect(await within(card).findByText(/Paused/i)).toBeInTheDocument();
    expect(within(card).queryByRole("button", { name: "Pause" })).not.toBeInTheDocument();

    fireEvent.click(within(card).getByRole("button", { name: "Resume" }));
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("resume_pomodoro"));
    expect(within(card).getByRole("button", { name: "Pause" })).toBeInTheDocument();
  });

  it("skips and restarts the phase in progress", async () => {
    const card = await startSessionAndTimer();

    fireEvent.click(within(card).getByRole("button", { name: "Skip phase" }));
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("skip_pomodoro_phase"));
    expect(await within(card).findByText("Short break")).toBeInTheDocument();

    fireEvent.click(within(card).getByRole("button", { name: "Restart phase" }));
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("restart_pomodoro_phase"));
  });

  it("waits for acknowledgement when a phase has ended, and offers nothing to skip", async () => {
    // The auto-start-off state: the phase is over, the next one has not begun, and the only
    // sensible action is to start it. Skip and restart would act on a phase that is not
    // running, so the card does not offer them.
    boundary.state.pomodoro = {
      running: true,
      paused: false,
      awaiting_acknowledgement: true,
      phase: "shortBreak",
      completed_work_intervals: 1,
      remaining_ms: 0,
    };
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });
    // The controls only exist while a session is active -- the timer belongs to a session.
    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByText("running");
    const card = pomodoroCard();

    expect(await within(card).findByText("Done")).toBeInTheDocument();
    expect(within(card).queryByRole("button", { name: "Skip phase" })).not.toBeInTheDocument();
    expect(within(card).queryByRole("button", { name: "Pause" })).not.toBeInTheDocument();

    fireEvent.click(within(card).getByRole("button", { name: /Start short break/i }));
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("acknowledge_pomodoro_phase"),
    );
    expect(await within(card).findByText("5:00")).toBeInTheDocument();
  });

  it("edits and saves the persisted Pomodoro rhythm", async () => {
    boundary.state.settings = {
      default_focus_mode: "normal",
      pomodoro: { work_ms: 1500000, short_break_ms: 300000, long_break_ms: 900000,
        intervals_before_long_break: 4, auto_start_next_phase: true },
    };
    render(<App />);
    const card = pomodoroCard();
    fireEvent.click(within(card).getByText("Customize rhythm"));
    fireEvent.change(within(card).getByLabelText("Work minutes"), { target: { value: "40" } });
    fireEvent.click(within(card).getByRole("button", { name: "Save rhythm" }));
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("set_pomodoro_config", {
      config: expect.objectContaining({ workMs: 40 * 60 * 1000 }),
    }));
  });
});
