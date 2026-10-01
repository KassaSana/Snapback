import { act, cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Mock the native boundary so the real api.ts + useSession run end to end.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    settings: Record<string, unknown>;
    autoLabel: string | null;
    autoLabelError: boolean;
    recording: Record<string, unknown>;
  } = {
    health: {},
    settings: {},
    autoLabel: "PRODUCTIVE",
    autoLabelError: false,
    recording: { state: "noSession", privatePauseRemainingMs: 0 },
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
      case "pause_recording_privately":
        state.recording = {
          state: "pausedPrivate",
          privatePauseRemainingMs: Number(args?.minutes ?? 0) * 60 * 1000,
        };
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
      case "stop_session":
        state.recording = { state: "noSession", privatePauseRemainingMs: 0 };
        return {
          sessionId: "sess-42",
          goal: "Write tests",
          status: "COMPLETED",
          focusMode: "normal",
          startedAtMs: Date.parse("2026-07-11T00:00:00Z"),
          endedAtMs: Date.parse("2026-07-11T00:30:00Z"),
        };
      case "get_session_recap":
        return { sessionId: "sess-42", goal: "Write tests", durationSecs: 1800, sampleCount: state.autoLabel === null ? 0 : 12 };
      case "get_session_auto_label":
        if (state.autoLabelError) throw new Error("label read failed");
        return state.autoLabel;
      case "get_analytics":
      case "get_focus_summary":
        return {};
      case "get_summary_report":
        return { window: "7d" };
      case "get_prediction_history":
      case "get_session_history":
      case "get_app_rules":
      case "get_context_timeline":
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
import { api } from "../src/api";

// Capture running so the first-run wizard stays out of the way.
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

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.settings = { defaultFocusMode: "normal" };
  boundary.state.autoLabel = "PRODUCTIVE";
  boundary.state.autoLabelError = false;
  boundary.state.recording = { state: "noSession", privatePauseRemainingMs: 0 };
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe("Session start/stop flow", () => {
  const reviewCallCount = () =>
    boundary.invoke.mock.calls.filter(([command, args]) => {
      if (
        command === "get_analytics" ||
        command === "get_summary_report" ||
        command === "get_focus_summary"
      ) {
        return true;
      }
      return command === "get_session_history" && args !== undefined && "window" in args;
    }).length;

  it("starts a session with the entered goal and reflects it in the UI", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });

    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));

    // The real api layer forwards the goal + focus mode to the command.
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("start_session", {
        goal: "Write tests",
        focusMode: "normal",
      }),
    );
    // UI reflects the running session. replaced "active" with running/paused:
    // a session that stopped counting attended time twenty minutes ago must not read the same
    // as one that is recording, which is the whole point of the distinction.
    //
    // Scoped to the Session Control card, because the health pill also says "running" and an
    // unscoped query would pass on the wrong element — reporting capture health as if it were
    // session state.
    expect(await screen.findByRole("heading", { name: "Focus state" })).toBeInTheDocument();
    const sessionCard = (await screen.findByRole("heading", { name: "Session Control" }))
      .closest("section") as HTMLElement;
    expect(within(sessionCard).getByText("running")).toBeInTheDocument();
  });

  it("keeps the header chip and Session Control on the same status vocabulary", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });
    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByRole("heading", { level: 1, name: "running" });

    const sessionCard = screen.getByRole("heading", { name: "Session Control" }).closest("section")!;
    const headerStatus = screen.getByRole("heading", { name: "Session status" }).closest("section")!;
    expect(within(sessionCard).getByText("running")).toBeInTheDocument();
    expect(within(headerStatus).getByText("running")).toBeInTheDocument();
    expect(within(headerStatus).getByText(/Elapsed and attended are both counting/)).toBeInTheDocument();
    expect(within(sessionCard).getByText(/Elapsed and attended are both counting/)).toBeInTheDocument();

    act(() => boundary.emit("idle", { idle: true }));
    await waitFor(() => {
      expect(within(sessionCard).getByText("Paused — no input")).toBeInTheDocument();
      expect(within(headerStatus).getByText("Paused — no input")).toBeInTheDocument();
    });
    expect(screen.getByRole("heading", { level: 1, name: "Paused — no input" })).toBeInTheDocument();

    act(() =>
      boundary.emit("recording-status", { state: "pausedPrivate", privatePauseRemainingMs: 0 }),
    );
    await waitFor(() => {
      expect(within(sessionCard).getByText("Paused — private")).toBeInTheDocument();
      expect(within(headerStatus).getByText("Paused — private")).toBeInTheDocument();
    });
    expect(screen.getByRole("heading", { level: 1, name: "Paused — private" })).toBeInTheDocument();
  });

  it("uses the persisted default focus mode for a new session", async () => {
    boundary.state.settings = { defaultFocusMode: "deep" };

    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });

    const select = screen.getByLabelText("Focus mode") as HTMLSelectElement;
    await waitFor(() => expect(select.value).toBe("deep"));

    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("start_session", {
        goal: "Write tests",
        focusMode: "deep",
      }),
    );
  });

  it("commits the drafted focus mode through Start, not through the select", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });

    // The select is a draft: choosing a mode writes nothing.
    const select = screen.getByLabelText("Focus mode") as HTMLSelectElement;
    fireEvent.change(select, { target: { value: "recovery" } });
    expect(select.value).toBe("recovery");
    expect(boundary.invoke).not.toHaveBeenCalledWith("set_focus_mode", expect.anything());

    // Start is the commit: the session opens in that mode and it becomes the default.
    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("start_session", {
        goal: "Write tests",
        focusMode: "recovery",
      }),
    );
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("set_focus_mode", { mode: "recovery" }),
    );
  });

  it("does not start a session when the goal is empty", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });

    fireEvent.click(screen.getByRole("button", { name: "Start session" }));

    // Give any (unexpected) async work a chance to run, then assert no call.
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_health"));
    expect(boundary.invoke).not.toHaveBeenCalledWith("start_session", expect.anything());
  });

  it("uses destination headings and puts session controls before live feedback", async () => {
    render(<App />);
    const controls = await screen.findByRole("heading", { name: "Session Control" });
    expect(screen.queryByRole("heading", { name: "Focus state" })).not.toBeInTheDocument();
    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    const feedback = await screen.findByRole("heading", { name: "Focus state" });
    expect(controls.compareDocumentPosition(feedback) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    fireEvent.click(screen.getByRole("tab", { name: "Review" }));
    expect(screen.getByRole("heading", { name: "Review your sessions" })).toBeInTheDocument();
    fireEvent.click(screen.getByRole("tab", { name: "Settings" }));
    expect(screen.getByRole("heading", { name: "Settings", level: 1 })).toBeInTheDocument();
  });

  it("stops an active session and shows it completed", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });

    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByRole("heading", { level: 1, name: "running" });

    fireEvent.click(screen.getByRole("button", { name: "Stop session" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("stop_session", { sessionId: "sess-42" }),
    );
    expect(await within(screen.getByRole("heading", { name: "Session Control" }).closest("section")!)
      .findByText("completed")).toBeInTheDocument();
    expect(await screen.findByRole("button", { name: "Keep Productive" })).toBeInTheDocument();
    expect(screen.getByText(/Automatic label: Productive/)).toBeInTheDocument();
  });

  it("does not claim a saved label when the lookup fails", async () => {
    boundary.state.autoLabelError = true;
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });
    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByRole("heading", { level: 1, name: "running" });
    fireEvent.click(screen.getByRole("button", { name: "Stop session" }));
    expect(await screen.findByText(/Automatic label unavailable/)).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Skip check-in" })).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /Keep automatic label/ })).toBeNull();
  });

  it("defers a completed-session Review refresh while Review is hidden", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });

    fireEvent.click(screen.getByRole("tab", { name: "Review" }));
    await waitFor(() => expect(reviewCallCount()).toBe(4));
    await waitFor(() => expect(screen.getByRole("button", { name: "Last 7 days" })).toBeEnabled());
    fireEvent.click(screen.getByRole("tab", { name: "Now" }));

    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByRole("heading", { level: 1, name: "running" });
    fireEvent.click(screen.getByRole("button", { name: "Stop session" }));
    await within(screen.getByRole("heading", { name: "Session Control" }).closest("section")!)
      .findByText("completed");

    expect(reviewCallCount()).toBe(4);
    fireEvent.click(screen.getByRole("tab", { name: "Review" }));
    await waitFor(() => expect(reviewCallCount()).toBe(8));
  });
});

async function stopTestSession() {
  render(<App />);
  await screen.findByRole("heading", { name: "Session Control" });
  fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), { target: { value: "Write tests" } });
  fireEvent.click(screen.getByRole("button", { name: "Start session" }));
  await screen.findByRole("heading", { level: 1, name: "running" });
  fireEvent.click(screen.getByRole("button", { name: "Stop session" }));
  await screen.findByRole("heading", { name: "Session Check-in" });
}

it("records Keep as explicit survey agreement and confirms it in the recap", async () => {
  await stopTestSession();
  fireEvent.click(screen.getByRole("button", { name: "Keep Productive" }));
  await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("submit_label", {
    request: { sessionId: "sess-42", label: "PRODUCTIVE", source: "survey", notes: "confirmed automatic label" },
  }));
  await waitFor(() => expect(screen.queryByRole("heading", { name: "Session Check-in" })).toBeNull());
  const recap = screen.getByRole("heading", { name: "Session Recap" }).closest("section")!;
  expect(within(recap).getByRole("status")).toHaveTextContent("Saved: Focused");
  expect(within(recap).getByRole("button", { name: "Change" })).toBeInTheDocument();
  fireEvent.click(within(recap).getByRole("button", { name: "Change" }));
  expect(await screen.findByRole("heading", { name: "Session Check-in" })).toBeInTheDocument();
  fireEvent.click(screen.getByRole("button", { name: "Keep Productive" }));
  await waitFor(() => expect(screen.queryByRole("heading", { name: "Session Check-in" })).toBeNull());
  fireEvent.click(screen.getByRole("button", { name: "Start session" }));
  await screen.findByRole("heading", { level: 1, name: "running" });
  expect(screen.queryByText(/Saved: Focused/)).toBeNull();
});

it("keeps the check-in open after a failed agreement and permits retry", async () => {
  await stopTestSession();
  const submit = vi.spyOn(api, "submitLabel").mockRejectedValueOnce(new Error("disk busy"));
  fireEvent.click(screen.getByRole("button", { name: "Keep Productive" }));
  expect(await screen.findByText("Could not save feedback. Try again.")).toBeInTheDocument();
  expect(screen.getByRole("heading", { name: "Session Check-in" })).toBeInTheDocument();
  fireEvent.click(screen.getByRole("button", { name: "Keep Productive" }));
  await waitFor(() => expect(screen.queryByRole("heading", { name: "Session Check-in" })).toBeNull());
  expect(submit).toHaveBeenCalledTimes(2);
});

it("distinguishes no signal from measured zero and skips without writing feedback", async () => {
  boundary.state.autoLabel = null;
  await stopTestSession();
  expect(screen.getByText(/No signal this session/)).toBeInTheDocument();
  const recap = screen.getByRole("heading", { name: "Session Recap" }).closest("section")!;
  expect(within(recap).getByText("No predictions recorded")).toBeInTheDocument();
  expect(within(recap).getAllByText("—")).toHaveLength(2);
  fireEvent.click(screen.getByRole("button", { name: "Skip check-in" }));
  await waitFor(() => expect(screen.queryByRole("heading", { name: "Session Check-in" })).toBeNull());
  expect(boundary.invoke.mock.calls.filter(([cmd]) => cmd === "submit_label")).toHaveLength(0);
});

it("guards Settings live feedback after stopping a session", async () => {
  await stopTestSession();
  fireEvent.click(screen.getByRole("tab", { name: "Settings" }));
  fireEvent.click(screen.getByRole("tab", { name: "Focus" }));
  const card = screen.getByRole("heading", { name: "Focus Feedback" }).closest("section")!;
  for (const button of within(card).getAllByRole("button")) {
    expect(button).toBeDisabled();
    fireEvent.click(button);
  }
  expect(boundary.invoke.mock.calls.filter(([cmd]) => cmd === "submit_label")).toHaveLength(0);
});

it("coalesces repeated Keep clicks while the survey save is pending", async () => {
  await stopTestSession();
  let resolve!: () => void;
  const submit = vi.spyOn(api, "submitLabel").mockImplementationOnce(() => new Promise<void>((done) => { resolve = done; }));
  const keep = screen.getByRole("button", { name: "Keep Productive" });
  fireEvent.click(keep);
  fireEvent.click(keep);
  expect(keep).toBeDisabled();
  expect(submit).toHaveBeenCalledTimes(1);
  await act(async () => resolve());
  expect(screen.queryByRole("heading", { name: "Session Check-in" })).toBeNull();
});

it("does not paint an old feedback save into the replacement session", async () => {
  await stopTestSession();
  let resolve!: () => void;
  vi.spyOn(api, "submitLabel").mockImplementationOnce(() => new Promise<void>((done) => { resolve = done; }));
  fireEvent.click(screen.getByRole("button", { name: "Keep Productive" }));
  fireEvent.click(screen.getByRole("button", { name: "Start session" }));
  await screen.findByRole("heading", { level: 1, name: "running" });
  await act(async () => resolve());
  expect(screen.queryByText("Session rating saved: Productive")).toBeNull();
});
