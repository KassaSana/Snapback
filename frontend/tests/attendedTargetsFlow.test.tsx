import { act, cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Drives the real card + hook + api against a mocked native boundary.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    settings: Record<string, unknown>;
    progress: Record<string, unknown>;
    savedTargets: Record<string, unknown> | null;
    recording: Record<string, unknown>;
  } = {
    health: {},
    settings: {},
    progress: {
      dailyTargetMins: 0,
      dailyActualMins: 0,
      weeklyTargetMins: 0,
      weeklyActualMins: 0,
    },
    savedTargets: null,
    recording: { state: "recording", privatePauseRemainingMs: 0 },
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
      case "get_recording_status":
        return state.recording;
      case "start_session":
        state.recording = { state: "recording", privatePauseRemainingMs: 0 };
        return {
          sessionId: "sess-42",
          goal: String(args?.goal ?? ""),
          status: "ACTIVE",
          focusMode: String(args?.focusMode ?? "normal"),
          startedAtMs: Date.parse("2026-07-11T00:00:00Z"),
          endedAtMs: null,
        };
      case "get_attended_progress":
        return state.progress;
      case "set_attended_targets":
        state.savedTargets = args ?? {};
        state.progress = {
          ...state.progress,
          dailyTargetMins: Number(args?.dailyMins ?? 0),
          weeklyTargetMins: Number(args?.weeklyMins ?? 0),
        };
        return state.progress;
      case "get_pomodoro_status":
        return { running: false, phase: "work", completedWorkIntervals: 0, remainingMs: 0 };
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
import { ATTENDED_REBASELINE_MS } from "../src/liveAttended";

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

const startSession = async () => {
  render(<App />);
  await screen.findByRole("heading", { name: "Session Control" });
  fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
    target: { value: "Write tests" },
  });
  fireEvent.click(screen.getByRole("button", { name: "Start session" }));
  await screen.findByRole("heading", { level: 1, name: "running" });
};

const targetsCard = () =>
  screen.getByRole("heading", { name: "Attended time" }).closest("section") as HTMLElement;

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.settings = { defaultFocusMode: "normal" };
  boundary.state.progress = {
    dailyTargetMins: 0,
    dailyActualMins: 0,
    weeklyTargetMins: 0,
    weeklyActualMins: 0,
  };
  boundary.state.savedTargets = null;
  boundary.state.recording = { state: "recording", privatePauseRemainingMs: 0 };
});

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

describe("Attended-time targets", () => {
  it("shows attendance with no target set, and offers to set one", async () => {
    boundary.state.progress = {
      dailyTargetMins: 0,
      dailyActualMins: 95,
      weeklyTargetMins: 0,
      weeklyActualMins: 320,
    };
    await startSession();
    const card = await screen.findByRole("heading", { name: "Attended time" });
    const section = card.closest("section") as HTMLElement;

    expect(await within(section).findByText(/1h 35m/)).toBeInTheDocument();
    expect(within(section).getByRole("button", { name: "Set a target" })).toBeInTheDocument();
  });

  it("reports progress against a target as a plain ratio", async () => {
    boundary.state.progress = {
      dailyTargetMins: 240,
      dailyActualMins: 120,
      weeklyTargetMins: 1200,
      weeklyActualMins: 300,
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
      dailyTargetMins: 240,
      dailyActualMins: 0,
      weeklyTargetMins: 0,
      weeklyActualMins: 0,
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

  it("shows Today <1m while accruing under a full minute", async () => {
    await startSession();
    const section = await screen.findByRole("heading", { name: "Attended time" }).then(
      (heading) => heading.closest("section") as HTMLElement,
    );
    expect(within(section).getByText(/Today <1m/)).toBeInTheDocument();
  });

  it("ticks local today minutes between baselines and freezes on idle", async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true });
    boundary.state.progress = {
      dailyTargetMins: 0,
      dailyActualMins: 2,
      weeklyTargetMins: 0,
      weeklyActualMins: 2,
    };
    await startSession();
    const section = targetsCard();
    expect(await within(section).findByText(/Today 2m/)).toBeInTheDocument();

    // One full minute of local accrual before the 60s rebaseline fires.
    await act(async () => {
      await vi.advanceTimersByTimeAsync(60_000);
    });
    // Local tick and rebaseline land together; mock still returns 2 unless we bump it.
    boundary.state.progress = {
      dailyTargetMins: 0,
      dailyActualMins: 3,
      weeklyTargetMins: 0,
      weeklyActualMins: 3,
    };
    await act(async () => {
      await vi.advanceTimersByTimeAsync(ATTENDED_REBASELINE_MS);
    });
    expect(await within(section).findByText(/Today 3m/)).toBeInTheDocument();

    const attendedCallsBeforeIdle = boundary.invoke.mock.calls.filter(
      ([cmd]) => cmd === "get_attended_progress",
    ).length;
    act(() => boundary.emit("idle", { idle: true }));
    await waitFor(() =>
      expect(
        boundary.invoke.mock.calls.filter(([cmd]) => cmd === "get_attended_progress").length,
      ).toBeGreaterThan(attendedCallsBeforeIdle),
    );

    // Idle freezes the local tick even if wall time keeps moving.
    await act(async () => {
      await vi.advanceTimersByTimeAsync(180_000);
    });
    expect(within(section).getByText(/Today 3m/)).toBeInTheDocument();
  });

  it("rebaselines on the slow cadence without 1Hz IPC", async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true });
    await startSession();
    const before = boundary.invoke.mock.calls.filter(([cmd]) => cmd === "get_attended_progress").length;
    await act(async () => {
      await vi.advanceTimersByTimeAsync(ATTENDED_REBASELINE_MS + 2_000);
    });
    const after = boundary.invoke.mock.calls.filter(([cmd]) => cmd === "get_attended_progress").length;
    // One slow rebaseline is fine; a 1 Hz poll would add ~60 calls.
    expect(after).toBeGreaterThan(before);
    expect(after - before).toBeLessThan(5);
  });
});
