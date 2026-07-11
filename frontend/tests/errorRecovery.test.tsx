import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { FIRST_RUN_ACK_KEY } from "../src/permissionWizardState";

// Mock the Tauri boundary; `startShouldFail` flips start_session into a reject.
const boundary = vi.hoisted(() => {
  const state: { health: Record<string, unknown>; startShouldFail: boolean } = {
    health: {},
    startShouldFail: false,
  };

  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
      case "start_session":
        if (state.startShouldFail) {
          throw new Error("capture backend unavailable");
        }
        return {
          session_id: "sess-1",
          goal: String(args?.goal ?? ""),
          status: "ACTIVE",
          focus_mode: "normal",
          started_at: "2026-07-11T00:00:00Z",
          ended_at: null,
        };
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

vi.mock("@tauri-apps/api/core", () => ({ invoke: boundary.invoke }));
vi.mock("@tauri-apps/api/event", () => ({ listen: boundary.listen }));

import App from "../src/App";

const health = (overrides: Record<string, unknown> = {}): Record<string, unknown> => ({
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
  ...overrides,
});

const startSessionWithGoal = async (goal: string) => {
  await screen.findByRole("heading", { name: "Session Control" });
  fireEvent.change(screen.getByPlaceholderText("Ship the snapback overlay"), {
    target: { value: goal },
  });
  fireEvent.click(screen.getByRole("button", { name: "Start session" }));
};

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = health();
  boundary.state.startShouldFail = false;
});

afterEach(() => {
  cleanup();
});

describe("Action error surfacing and recovery", () => {
  it("shows a visible error when starting a session fails, and dismisses it", async () => {
    boundary.state.startShouldFail = true;
    render(<App />);

    await startSessionWithGoal("Write tests");

    const alert = await screen.findByRole("alert");
    expect(alert).toHaveTextContent(/Could not start session/i);

    fireEvent.click(screen.getByRole("button", { name: "Dismiss" }));
    await waitFor(() => expect(screen.queryByRole("alert")).not.toBeInTheDocument());
  });

  it("warns before a session when capture is unavailable, without blocking start", async () => {
    // Wizard acknowledged so it doesn't cover the screen; capture is down.
    window.localStorage.setItem(FIRST_RUN_ACK_KEY, "true");
    boundary.state.health = health({
      capture_running: false,
      permissions: {
        capture_available: false,
        capture_probe_confirmed: false,
        active_window_available: false,
        message: "Grant Input Monitoring.",
        setup_steps: [],
      },
    });
    render(<App />);

    await startSessionWithGoal("Write tests");

    // The session still starts (warn, don't block) but the risk is surfaced.
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("start_session", {
        goal: "Write tests",
        focusMode: "normal",
      }),
    );
    expect(await screen.findByRole("alert")).toHaveTextContent(/input capture isn't available/i);
  });
});
