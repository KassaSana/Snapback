import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Roadmap 2.14. Mocks only the native boundary, so the real card + useSession + api.ts run
// end to end: a field wired to the wrong argument fails here rather than in the app.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    settings: Record<string, unknown>;
    saved: Record<string, unknown> | null;
  } = { health: {}, settings: {}, saved: null };

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
          focus_mode: "normal",
          started_at_ms: Date.parse("2026-07-11T00:00:00Z"),
          ended_at_ms: null,
        };
      case "stop_session":
        return {
          session_id: "sess-42",
          goal: "Write tests",
          status: "COMPLETED",
          focus_mode: "normal",
          started_at_ms: Date.parse("2026-07-11T00:00:00Z"),
          ended_at_ms: Date.parse("2026-07-11T00:30:00Z"),
        };
      case "get_session_recap":
        return { session_id: "sess-42", goal: "Write tests", duration_secs: 1800 };
      case "save_session_reflection":
        state.saved = args ?? {};
        return {
          session_id: "sess-42",
          goal: "Write tests",
          status: "COMPLETED",
          focus_mode: "normal",
          reflection_done: args?.done ?? null,
          reflection_next_step: args?.nextStep ?? null,
        };
      case "get_prediction_history":
      case "get_app_rules":
      case "get_context_timeline":
      case "get_session_history":
        return [];
      case "get_pomodoro_status":
        return { running: false, phase: "work", completed_work_intervals: 0, remaining_ms: 0 };
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

const reflectionCard = () =>
  screen.getByRole("heading", { name: "Reflection" }).closest("section") as HTMLElement;

// Runs a whole session so the end-of-session prompt is on screen, which is the only place
// this card appears.
const runASession = async () => {
  render(<App />);
  await screen.findByRole("heading", { name: "Session Control" });
  fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
    target: { value: "Write tests" },
  });
  fireEvent.click(screen.getByRole("button", { name: "Start session" }));
  await screen.findByText("running");
  fireEvent.click(screen.getByRole("button", { name: "Stop session" }));
  await screen.findByText("completed");
  await screen.findByRole("heading", { name: "Reflection" });
};

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.settings = { default_focus_mode: "normal" };
  boundary.state.saved = null;
});

afterEach(() => {
  cleanup();
});

describe("Session reflection", () => {
  it("is not offered until a session has ended", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });
    fireEvent.click(screen.getByRole("tab", { name: "Review" }));
    expect(screen.queryByRole("heading", { name: "Reflection" })).not.toBeInTheDocument();
  });

  it("saves both answers against the session that just ended", async () => {
    await runASession();
    const card = reflectionCard();

    fireEvent.change(within(card).getByLabelText("What got done?"), {
      target: { value: "  wired the CSV path  " },
    });
    fireEvent.change(within(card).getByLabelText("Next step"), {
      target: { value: "add the header row" },
    });
    fireEvent.click(within(card).getByRole("button", { name: "Save reflection" }));

    await waitFor(() => expect(boundary.state.saved).not.toBeNull());
    // Trimmed on the way out, and sent against the completed session's id.
    expect(boundary.state.saved).toEqual({
      sessionId: "sess-42",
      done: "wired the CSV path",
      nextStep: "add the header row",
    });
    expect(await within(card).findByText(/Kept with the session/i)).toBeInTheDocument();
  });

  it("sends null for the half the user left blank", async () => {
    await runASession();
    const card = reflectionCard();

    fireEvent.change(within(card).getByLabelText("Next step"), {
      target: { value: "start the draft" },
    });
    fireEvent.click(within(card).getByRole("button", { name: "Save reflection" }));

    await waitFor(() => expect(boundary.state.saved).not.toBeNull());
    expect(boundary.state.saved).toEqual({
      sessionId: "sess-42",
      done: null,
      nextStep: "start the draft",
    });
  });

  it("cannot save nothing: blank and whitespace are not answers", async () => {
    await runASession();
    const card = reflectionCard();

    expect(within(card).getByRole("button", { name: "Save reflection" })).toBeDisabled();
    fireEvent.change(within(card).getByLabelText("What got done?"), {
      target: { value: "   \t  " },
    });
    expect(within(card).getByRole("button", { name: "Save reflection" })).toBeDisabled();
  });

  it("skips in one click and writes nothing at all", async () => {
    // 2.14's promise: a skipped reflection and one that was never offered are the same absent
    // state, so Skip must not reach the backend.
    await runASession();
    const card = reflectionCard();

    fireEvent.click(within(card).getByRole("button", { name: "Skip" }));

    await waitFor(() =>
      expect(screen.queryByRole("heading", { name: "Reflection" })).not.toBeInTheDocument(),
    );
    expect(boundary.state.saved).toBeNull();
    expect(boundary.invoke).not.toHaveBeenCalledWith(
      "save_session_reflection",
      expect.anything(),
    );
  });
});
