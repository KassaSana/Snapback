import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Mock the Tauri boundary so the real api.ts + useSession run end to end.
const boundary = vi.hoisted(() => {
  const state: { health: Record<string, unknown> } = { health: {} };

  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
      case "start_session":
        return {
          session_id: "sess-42",
          goal: String(args?.goal ?? ""),
          status: "ACTIVE",
          focus_mode: String(args?.focusMode ?? "normal"),
          started_at: "2026-07-11T00:00:00Z",
          ended_at: null,
        };
      case "stop_session":
        return {
          session_id: "sess-42",
          goal: "Write tests",
          status: "COMPLETED",
          focus_mode: "normal",
          started_at: "2026-07-11T00:00:00Z",
          ended_at: "2026-07-11T00:30:00Z",
        };
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

vi.mock("@tauri-apps/api/core", () => ({ invoke: boundary.invoke }));
vi.mock("@tauri-apps/api/event", () => ({ listen: boundary.listen }));

import App from "../src/App";

// Capture running so the first-run wizard stays out of the way.
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

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
});

afterEach(() => {
  cleanup();
});

describe("Session start/stop flow", () => {
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
    // UI reflects the active session.
    expect(await screen.findByText("sess-42")).toBeInTheDocument();
    expect(await screen.findByText("active")).toBeInTheDocument();
  });

  it("does not start a session when the goal is empty", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });

    fireEvent.click(screen.getByRole("button", { name: "Start session" }));

    // Give any (unexpected) async work a chance to run, then assert no call.
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_health"));
    expect(boundary.invoke).not.toHaveBeenCalledWith("start_session", expect.anything());
  });

  it("stops an active session and shows it completed", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });

    fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
      target: { value: "Write tests" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByText("active");

    fireEvent.click(screen.getByRole("button", { name: "Stop session" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("stop_session", { sessionId: "sess-42" }),
    );
    expect(await screen.findByText("completed")).toBeInTheDocument();
  });
});
