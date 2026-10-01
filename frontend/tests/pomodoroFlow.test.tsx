import { act, cleanup, fireEvent, render, renderHook, screen, waitFor, within } from "@testing-library/react";
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
    pomodoro: { running: false, phase: "work", completedWorkIntervals: 0, remainingMs: 0 },
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
          sessionId: "sess-42",
          goal: String(args?.goal ?? ""),
          status: "ACTIVE",
          focusMode: String(args?.focusMode ?? "normal"),
          startedAtMs: Date.parse("2026-07-11T00:00:00Z"),
          endedAtMs: null,
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
          completedWorkIntervals: 0,
          remainingMs: 25 * 60 * 1000,
        };
        return state.pomodoro;
      case "stop_pomodoro":
        state.pomodoro = {
          running: false,
          phase: "work",
          completedWorkIntervals: 0,
          remainingMs: 0,
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
          remainingMs: 5 * 60 * 1000,
        };
        return state.pomodoro;
      case "restart_pomodoro_phase":
        state.pomodoro = { ...state.pomodoro, remainingMs: 25 * 60 * 1000 };
        return state.pomodoro;
      case "acknowledge_pomodoro_phase":
        state.pomodoro = {
          ...state.pomodoro,
          awaitingAcknowledgement: false,
          phase: "shortBreak",
          remainingMs: 5 * 60 * 1000,
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
import { usePomodoro, usePomodoroCountdown } from "../src/usePomodoro";
import type { PomodoroStatus } from "../src/api";

const healthyCaptureRunning = (): Record<string, unknown> => ({
  status: "online",
  captureRunning: true,
  captureFailed: false,
  captureEventsDropped: 0,
  permissions: {
    captureAvailable: true,
    captureProbeConfirmed: true,
    activeWindowAvailable: true,
    message: "",
    setupSteps: [],
  },
  classifier: { backend: "heuristic", onnxRuntimeEnabled: false, modelPath: null },
});

const pomodoroCard = () =>
  screen.getByRole("heading", { name: "Pomodoro" }).closest("section") as HTMLElement;

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.settings = { defaultFocusMode: "normal" };
  boundary.state.settingsThrows = false;
  boundary.state.pomodoro = {
    running: false,
    phase: "work",
    completedWorkIntervals: 0,
    remainingMs: 0,
  };
});

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

describe("Pomodoro card", () => {
  it("stays off the idle Now surface until a session is running", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });
    expect(screen.queryByRole("heading", { name: "Pomodoro" })).not.toBeInTheDocument();
  });

  it("starts and stops the timer once a session is active", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });

    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByRole("heading", { level: 1, name: "running" });  // running/paused, not "active"

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
      completedWorkIntervals: 0,
      remainingMs: 12 * 60 * 1000,
    };

    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });
    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByRole("heading", { level: 1, name: "running" });
    const card = pomodoroCard();
    expect(await within(card).findByText("12:00")).toBeInTheDocument();
  });

  // Drives the real card + usePomodoro + api.ts against the mocked boundary,
  // so a control that is wired to the wrong command fails here rather than in the app.
  const startSessionAndTimer = async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });
    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByRole("heading", { level: 1, name: "running" });
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
      awaitingAcknowledgement: true,
      phase: "shortBreak",
      completedWorkIntervals: 1,
      remainingMs: 0,
    };
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });
    // The controls only exist while a session is active -- the timer belongs to a session.
    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByRole("heading", { level: 1, name: "running" });
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
      defaultFocusMode: "normal",
      pomodoro: { workMs: 1500000, shortBreakMs: 300000, longBreakMs: 900000,
        intervalsBeforeLongBreak: 4, autoStartNextPhase: true },
    };
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });
    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByRole("heading", { name: "Pomodoro" });
    const card = pomodoroCard();
    fireEvent.click(within(card).getByText("Customize rhythm"));
    const workInput = within(card).getByLabelText("Work minutes");
    await waitFor(() => expect(workInput).toHaveValue(25));
    fireEvent.change(workInput, { target: { value: "40" } });
    expect(workInput).toHaveValue(40);
    fireEvent.click(within(card).getByRole("button", { name: "Save rhythm" }));
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("set_pomodoro_config", {
      config: expect.objectContaining({ workMs: 40 * 60 * 1000 }),
    }));
  });
});


describe("Pomodoro snapshot countdown", () => {
  const running: PomodoroStatus = { running: true, paused: false, awaitingAcknowledgement: false,
    phase: "work", completedWorkIntervals: 0, remainingMs: 10_000 };

  it("counts down monotonically, freezes during pause, and reanchors on resume", () => {
    vi.useFakeTimers({ toFake: ["setInterval", "clearInterval", "performance"] });
    const { result, rerender, unmount } = renderHook(({ status }) => usePomodoroCountdown(status),
      { initialProps: { status: running } });
    act(() => { vi.advanceTimersByTime(3000); });
    expect(result.current.remainingMs).toBe(7000);
    rerender({ status: { ...running, paused: true, remainingMs: 7000 } });
    act(() => { vi.advanceTimersByTime(30_000); });
    expect(result.current.remainingMs).toBe(7000);
    rerender({ status: { ...running, remainingMs: 7000 } });
    act(() => { vi.advanceTimersByTime(2000); });
    expect(result.current.remainingMs).toBe(5000);
    rerender({ status: { ...running, phase: "shortBreak", remainingMs: 5000 } });
    expect(result.current.phase).toBe("shortBreak");
    expect(result.current.remainingMs).toBe(5000);
    unmount();
    expect(vi.getTimerCount()).toBe(0);
  });

  it("clamps at zero without inventing a phase and stops ticking while awaiting", () => {
    vi.useFakeTimers({ toFake: ["setInterval", "clearInterval", "performance"] });
    const { result, rerender } = renderHook(({ status }) => usePomodoroCountdown(status),
      { initialProps: { status: running } });
    act(() => { vi.advanceTimersByTime(60_000); });
    expect(result.current.remainingMs).toBe(0);
    expect(result.current.phase).toBe("work");
    expect(vi.getTimerCount()).toBe(0);
    rerender({ status: { ...running, phase: "shortBreak", awaitingAcknowledgement: true, remainingMs: 0 } });
    act(() => { vi.advanceTimersByTime(60_000); });
    expect(result.current.remainingMs).toBe(0);
    expect(vi.getTimerCount()).toBe(0);
  });

  it("ignores an old status read arriving after a phase event", async () => {
    let resolveRead!: (value: unknown) => void;
    boundary.invoke.mockImplementationOnce(() => new Promise((resolve) => { resolveRead = resolve; }));
    const { result } = renderHook(() => usePomodoro({ setActionError: () => {} }));
    let refresh!: Promise<void>;
    act(() => { refresh = result.current.refreshPomodoroStatus(); });
    act(() => result.current.handlePomodoroEvent({ ...running, phase: "shortBreak", remainingMs: 5000 }));
    await act(async () => { resolveRead(running); await refresh; });
    expect(result.current.pomodoroStatus.phase).toBe("shortBreak");
    expect(result.current.pomodoroStatus.remainingMs).toBe(5000);
  });
});
