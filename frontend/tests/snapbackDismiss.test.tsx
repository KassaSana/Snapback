import { act, cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { FIRST_RUN_ACK_KEY } from "../src/permissionWizardState";

// Mock the Tauri boundary so the real api.ts + useLiveData run end to end. Unlike the
// other flow tests, this one needs to actually fire a subscribed event handler (the
// "snapback" listener), so the listen mock records handlers instead of being a no-op.
const boundary = vi.hoisted(() => {
  const state: { health: Record<string, unknown> } = { health: {} };
  const listeners: Record<string, Array<(event: { payload: unknown }) => void>> = {};

  const invoke = vi.fn(async (cmd: string): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
      case "dismiss_snapback":
        return null;
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

  const listen = vi.fn(
    async (event: string, handler: (e: { payload: unknown }) => void) => {
      (listeners[event] ??= []).push(handler);
      return () => {};
    },
  );

  const emit = (event: string, payload: unknown) => {
    for (const handler of listeners[event] ?? []) handler({ payload });
  };

  return { state, invoke, listen, emit };
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

beforeEach(() => {
  window.localStorage.clear();
  window.localStorage.setItem(FIRST_RUN_ACK_KEY, "true");
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
});

afterEach(() => {
  cleanup();
});

describe("Snapback note dismiss", () => {
  it("shows the note on a snapback event and clears it via dismiss_snapback", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Live Prediction" });

    act(() => {
      boundary.emit("snapback", { summary: "Return to auth.ts" });
    });

    expect(await screen.findByText(/Snapback: Return to auth\.ts/)).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Dismiss" }));

    // The command's real job is unsticking ContextTracker's Recovering state server-side
    // (see AppState::dismiss_snapback); the frontend just has to actually call it.
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("dismiss_snapback"));
    expect(screen.queryByText(/Snapback: Return to auth\.ts/)).not.toBeInTheDocument();
  });
});
