import {
  act,
  cleanup,
  fireEvent,
  render,
  renderHook,
  screen,
  waitFor,
  within,
} from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Roadmap 2.11. Drives the real SessionControlCard + useSession + api.ts against the mocked
// native boundary, so a control wired to the wrong command fails here rather than in the app.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    settings: Record<string, unknown>;
    history: Record<string, unknown>[];
    /** When set, `start_session` blocks on this until the test releases it. */
    holdStart: null | { promise: Promise<void>; release: () => void };
    /** When set, `start_session` rejects -- after the stop half of a switch has succeeded. */
    failStart: boolean;
    /** Started sessions so far; the first is `sess-42`, then `sess-43`, ... */
    started: number;
  } = {
    health: {},
    settings: {},
    history: [],
    holdStart: null,
    failStart: false,
    started: 0,
  };

  const session = (overrides: Record<string, unknown> = {}) => ({
    session_id: "sess-42",
    goal: "Write tests",
    status: "ACTIVE",
    focus_mode: "normal",
    started_at_ms: Date.parse("2026-07-11T00:00:00Z"),
    ended_at_ms: null,
    ...overrides,
  });

  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
      case "get_settings":
        return state.settings;
      case "get_session_history":
        return state.history;
      case "start_session":
        if (state.holdStart) await state.holdStart.promise;
        if (state.failStart) throw new Error("capture down");
        state.started += 1;
        return session({
          session_id: `sess-${41 + state.started}`,
          goal: String(args?.goal ?? ""),
          focus_mode: String(args?.focusMode ?? "normal"),
        });
      case "stop_session":
        return session({
          session_id: String(args?.sessionId ?? "sess-42"),
          status: "COMPLETED",
          ended_at_ms: Date.parse("2026-07-11T00:30:00Z"),
        });
      case "get_session_recap":
        return { session_id: "sess-42", goal: "Write tests", duration_secs: 1800 };
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
import { useSession } from "../src/useSession";

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

const historyRow = (goal: string, focusMode: string) => ({
  record: {
    session_id: `sess-${goal}`,
    goal,
    status: "COMPLETED",
    focus_mode: focusMode,
    started_at_ms: Date.parse("2026-07-10T09:00:00Z"),
    ended_at_ms: Date.parse("2026-07-10T10:00:00Z"),
  },
  recap: {},
});

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.settings = { default_focus_mode: "normal" };
  boundary.state.history = [];
  boundary.state.holdStart = null;
  boundary.state.failStart = false;
  boundary.state.started = 0;
});

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

const sessionCard = async () =>
  (await screen.findByRole("heading", { name: "Session Control" })).closest(
    "section",
  ) as HTMLElement;

const goalField = () => screen.getByPlaceholderText("Ship the snapback overlay");

const startedSessions = () =>
  boundary.invoke.mock.calls.filter(([cmd]) => cmd === "start_session").length;

describe("session cockpit", () => {
  it("explains a blank goal instead of silently doing nothing", async () => {
    render(<App />);
    const card = await sessionCard();

    // Nothing is red before the user has touched anything — but Start is already dead, which
    // is the half that used to be missing.
    expect(within(card).queryByRole("alert")).toBeNull();
    const start = within(card).getByRole("button", { name: "Start session" });
    expect(start).toBeDisabled();

    // Typing and then clearing turns the silence into an explanation.
    fireEvent.change(goalField(), { target: { value: "x" } });
    expect(within(card).getByRole("button", { name: "Start session" })).toBeEnabled();
    fireEvent.change(goalField(), { target: { value: "   " } });

    const error = within(card).getByRole("alert");
    expect(error).toHaveTextContent("Name what you're working on");
    expect(within(card).getByRole("button", { name: "Start session" })).toBeDisabled();
    expect(goalField()).toHaveAttribute("aria-invalid", "true");
    expect(startedSessions()).toBe(0);
  });

  it("starts on Enter from the goal field", async () => {
    render(<App />);
    await sessionCard();

    fireEvent.change(goalField(), { target: { value: "Write tests" } });
    // submit rather than a click: this is the keypress path, which previously did nothing.
    fireEvent.submit(goalField().closest("form") as HTMLFormElement);

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("start_session", {
        goal: "Write tests",
        focusMode: "normal",
      }),
    );
  });

  // THE RULE this item exists for: one intent, one session row.
  //
  // Two layers enforce it and they are tested separately on purpose. This case covers the UI
  // layer — once a request is in flight the controls are inert, including the Enter path, which
  // a `disabled` attribute alone does not stop.
  it("issues one start_session for a double click", async () => {
    let release = () => {};
    const promise = new Promise<void>((resolve) => {
      release = resolve;
    });
    boundary.state.holdStart = { promise, release };

    render(<App />);
    const card = await sessionCard();

    fireEvent.change(goalField(), { target: { value: "Write tests" } });
    const start = within(card).getByRole("button", { name: "Start session" });

    fireEvent.click(start);
    // The request is in flight and the control says so rather than looking idle.
    await waitFor(() => expect(startedSessions()).toBe(1));
    fireEvent.click(start);
    fireEvent.click(start);
    // Enter too — a disabled button does not stop a form submit.
    fireEvent.submit(goalField().closest("form") as HTMLFormElement);

    release();
    await waitFor(() => expect(within(card).getByText("running")).toBeInTheDocument());
    expect(startedSessions()).toBe(1);
  });

  // The second layer. `sessionPending` is React state, so it cannot disable anything until
  // React re-renders; two calls landing in the *same* turn both see the old value. That is
  // reachable from any non-DOM caller — a hotkey, a tray action, the switch path — so the hook
  // is guarded by a ref that is read and written synchronously. Asserted here rather than
  // through the DOM because the card's own gating would mask it and prove nothing.
  it("issues one start_session when the handler is called twice in one turn", async () => {
    let release = () => {};
    const promise = new Promise<void>((resolve) => {
      release = resolve;
    });
    boundary.state.holdStart = { promise, release };

    const noop = () => {};
    const { result } = renderHook(() =>
      useSession({
        refreshContextTimeline: noop,
        resetTimelineRefreshGate: noop,
        clearSessionLiveSignals: noop,
        setActionError: noop,
        setLabelStatus: noop,
        setLabelStatusWarning: noop,
      }),
    );

    act(() => result.current.setSessionGoal("Write tests"));

    await act(async () => {
      // Deliberately not awaited between calls: this is the window the ref exists to close.
      void result.current.handleStartSession();
      void result.current.handleStartSession();
      release();
    });

    expect(startedSessions()).toBe(1);
  });

  it("offers recent goals as chips and Start last session starts that session", async () => {
    boundary.state.history = [
      historyRow("Ship the overlay", "deep"),
      historyRow("Answer email", "normal"),
    ];

    render(<App />);
    const card = await sessionCard();
    await within(card).findByRole("button", { name: "Start last session" });

    fireEvent.click(within(card).getByRole("button", { name: "Start last session" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("start_session", {
        goal: "Ship the overlay",
        focusMode: "deep",
      }),
    );
    expect(startedSessions()).toBe(1);
  });

  it("fills a recent-goal chip without starting a session", async () => {
    boundary.state.history = [
      historyRow("Ship the overlay", "deep"),
      historyRow("Answer email", "normal"),
    ];

    render(<App />);
    const card = await sessionCard();
    fireEvent.click(within(card).getByRole("button", { name: "Answer email" }));
    expect(goalField()).toHaveValue("Answer email");
    expect(startedSessions()).toBe(0);
  });

  it("pins a goal, reorders it, and never starts it on click", async () => {
    render(<App />);
    const card = await sessionCard();

    fireEvent.change(goalField(), { target: { value: "Ship the overlay" } });
    fireEvent.click(within(card).getByRole("button", { name: "Pin this goal" }));
    fireEvent.change(goalField(), { target: { value: "Answer email" } });
    fireEvent.click(within(card).getByRole("button", { name: "Pin this goal" }));

    const pinned = () =>
      within(card)
        .getAllByRole("button", { name: /· (Deep|Normal|Recovery)$/ })
        .map((node) => node.textContent);
    expect(pinned()).toEqual(["Ship the overlay · Normal", "Answer email · Normal"]);

    fireEvent.click(within(card).getByRole("button", { name: "Move Answer email up" }));
    expect(pinned()).toEqual(["Answer email · Normal", "Ship the overlay · Normal"]);

    // Applying a pin fills the form and stops there.
    fireEvent.change(goalField(), { target: { value: "" } });
    fireEvent.click(within(card).getByRole("button", { name: "Ship the overlay · Normal" }));
    expect(goalField()).toHaveValue("Ship the overlay");
    expect(startedSessions()).toBe(0);

    fireEvent.click(within(card).getByRole("button", { name: "Unpin Answer email" }));
    expect(pinned()).toEqual(["Ship the overlay · Normal"]);
  });

  it("leads with elapsed time and keeps the session id as technical detail", async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true });
    vi.setSystemTime(new Date(Date.parse("2026-07-11T00:12:34Z")));

    render(<App />);
    const card = await sessionCard();

    fireEvent.change(goalField(), { target: { value: "Write tests" } });
    fireEvent.click(within(card).getByRole("button", { name: "Start session" }));
    await within(card).findByText("running");

    // Elapsed is derived from the backend's started_at, not from a counter that began at zero
    // when this component mounted.
    expect(within(card).getByLabelText("Elapsed session time")).toHaveTextContent("12m 34s");

    // The UUID is still reachable for support, but is no longer the headline.
    const details = within(card).getByText("Technical details").closest("details") as HTMLElement;
    expect(within(details).getByText("sess-42")).toBeInTheDocument();
  });

  it("guards switching sessions and stops the old one before starting the new", async () => {
    render(<App />);
    const card = await sessionCard();

    fireEvent.change(goalField(), { target: { value: "Write tests" } });
    fireEvent.click(within(card).getByRole("button", { name: "Start session" }));
    await within(card).findByText("running");

    // While running there is exactly one Stop and no second start form.
    expect(screen.queryByPlaceholderText("Ship the snapback overlay")).toBeNull();

    fireEvent.click(within(card).getByRole("button", { name: "Start a different session" }));
    // The consequence is stated before the user commits to it.
    expect(within(card).getByRole("status")).toHaveTextContent("stops this one first");

    fireEvent.change(goalField(), { target: { value: "Review the PR" } });
    fireEvent.click(within(card).getByRole("button", { name: "Stop and start this one" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("stop_session", { sessionId: "sess-42" }),
    );
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("start_session", {
        goal: "Review the PR",
        focusMode: "normal",
      }),
    );
  });

  // Slice 4 of the Astra review (Roadmap 2.11 / 14.4): the switch interaction has to leave
  // the UI describing what storage actually holds, and drafting one must not touch the
  // session that is running.
  it("drafting a replacement changes nothing native, and Keep this session restores the draft", async () => {
    render(<App />);
    const card = await sessionCard();
    fireEvent.change(goalField(), { target: { value: "Write tests" } });
    fireEvent.click(within(card).getByRole("button", { name: "Start session" }));
    await within(card).findByText("running");
    boundary.invoke.mockClear();

    fireEvent.click(within(card).getByRole("button", { name: "Start a different session" }));
    fireEvent.change(goalField(), { target: { value: "Review the PR" } });
    fireEvent.change(screen.getByLabelText("Focus mode"), { target: { value: "deep" } });

    // The running session is untouched: no live/default mode write, and its card still says
    // what it was started with.
    expect(boundary.invoke).not.toHaveBeenCalledWith("set_focus_mode", expect.anything());
    const metrics = card.querySelector(".metrics") as HTMLElement;
    expect(within(metrics).getByText("Write tests")).toBeInTheDocument();
    expect(within(metrics).getByText("Normal")).toBeInTheDocument();

    fireEvent.click(within(card).getByRole("button", { name: "Keep this session" }));
    await waitFor(() =>
      expect(screen.queryByPlaceholderText("Ship the snapback overlay")).toBeNull(),
    );
    expect(boundary.invoke).not.toHaveBeenCalledWith("set_focus_mode", expect.anything());

    // Reopening the draft shows the session as it is, not the abandoned edits.
    fireEvent.click(within(card).getByRole("button", { name: "Start a different session" }));
    expect((goalField() as HTMLInputElement).value).toBe("Write tests");
    expect((screen.getByLabelText("Focus mode") as HTMLSelectElement).value).toBe("normal");
  });

  it("shows the stopped session when the replacement start fails", async () => {
    render(<App />);
    const card = await sessionCard();
    fireEvent.change(goalField(), { target: { value: "Write tests" } });
    fireEvent.click(within(card).getByRole("button", { name: "Start session" }));
    await within(card).findByText("running");

    boundary.state.failStart = true;
    fireEvent.click(within(card).getByRole("button", { name: "Start a different session" }));
    fireEvent.change(goalField(), { target: { value: "Review the PR" } });
    fireEvent.click(within(card).getByRole("button", { name: "Stop and start this one" }));

    // Storage completed the old session, so the UI says completed -- not "running" over a
    // row that no longer is -- and explains which half happened.
    await within(card).findByText("completed");
    expect(await screen.findByText(/Stopped the previous session/)).toBeInTheDocument();
    expect(within(card).queryByRole("button", { name: "Stop session" })).toBeNull();

    // The switch interaction is over: the ordinary Start form is usable without a remount.
    boundary.state.failStart = false;
    expect((goalField() as HTMLInputElement).value).toBe("Review the PR");
    const start = within(card).getByRole("button", { name: "Start session" });
    await waitFor(() => expect(start).toBeEnabled());
    fireEvent.click(start);
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("start_session", {
        goal: "Review the PR",
        focusMode: "normal",
      }),
    );
  });

  it("switches, then stops and starts again without a remount", async () => {
    render(<App />);
    const card = await sessionCard();
    fireEvent.change(goalField(), { target: { value: "Write tests" } });
    fireEvent.click(within(card).getByRole("button", { name: "Start session" }));
    await within(card).findByText("running");

    fireEvent.click(within(card).getByRole("button", { name: "Start a different session" }));
    fireEvent.change(goalField(), { target: { value: "Review the PR" } });
    fireEvent.click(within(card).getByRole("button", { name: "Stop and start this one" }));
    await within(card).findByText("Review the PR");
    // The switch form closed on its own once the new session was running.
    await waitFor(() =>
      expect(screen.queryByPlaceholderText("Ship the snapback overlay")).toBeNull(),
    );
    expect(within(card).getByRole("button", { name: "Start a different session" })).toBeEnabled();

    fireEvent.click(within(card).getByRole("button", { name: "Stop session" }));
    await within(card).findByText("completed");
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("stop_session", { sessionId: "sess-43" }),
    );

    // The start form is the plain one, and it works: the "switching" flag did not survive
    // into a state where its precondition (an active session) can never hold.
    fireEvent.change(goalField(), { target: { value: "Third thing" } });
    const start = within(card).getByRole("button", { name: "Start session" });
    await waitFor(() => expect(start).toBeEnabled());
    fireEvent.click(start);
    await waitFor(() => expect(startedSessions()).toBe(3));
  });

  it("navigates and selects goal suggestions via keyboard and mouse", async () => {
    boundary.state.history = [
      historyRow("Ship the overlay", "deep"),
      historyRow("Answer email", "normal"),
    ];

    render(<App />);
    const card = await sessionCard();
    const input = goalField();

    // Focusing the input opens suggestions
    fireEvent.focus(input);
    const dropdown = await within(card).findByRole("listbox", { name: "Suggested goals" });
    expect(dropdown).toBeInTheDocument();

    const options = within(dropdown).getAllByRole("option");
    expect(options).toHaveLength(2);
    expect(options[0]).toHaveTextContent("Ship the overlay");

    // Arrow down highlights the first suggestion
    fireEvent.keyDown(input, { key: "ArrowDown" });
    expect(options[0]).toHaveAttribute("aria-selected", "true");

    // Enter applies the highlighted suggestion
    fireEvent.keyDown(input, { key: "Enter" });
    expect(input).toHaveValue("Ship the overlay");
    expect((screen.getByLabelText("Focus mode") as HTMLSelectElement).value).toBe("deep");

    // Clicking a suggestion item directly
    fireEvent.focus(input);
    fireEvent.change(input, { target: { value: "Ans" } });
    const emailOption = await within(card).findByRole("option", { name: /Answer email/i });
    fireEvent.mouseDown(emailOption);
    expect(input).toHaveValue("Answer email");
  });
});

