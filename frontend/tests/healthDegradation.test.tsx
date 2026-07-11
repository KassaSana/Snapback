import { act, cleanup, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { HEALTH_POLL_MS } from "../src/healthPoll";
import { FIRST_RUN_ACK_KEY } from "../src/permissionWizardState";

const boundary = vi.hoisted(() => {
  const state: { health: Record<string, unknown> } = { health: {} };
  const invoke = vi.fn(async (cmd: string): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
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
  capture_failure_reason: null,
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

const permissionsCard = () =>
  screen.getByRole("heading", { name: "Permissions" }).closest("section") as HTMLElement;

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = health();
});

afterEach(() => {
  cleanup();
});

describe("Health degradation visibility", () => {
  it("warns when capture events are being dropped", async () => {
    boundary.state.health = health({ capture_events_dropped: 5 });
    render(<App />);

    const card = within(permissionsCard());
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_health"));
    expect(await card.findByText(/5 capture events dropped/i)).toBeInTheDocument();
  });

  it("shows the capture-failed state with its reason", async () => {
    // Wizard acknowledged so it doesn't cover the screen.
    window.localStorage.setItem(FIRST_RUN_ACK_KEY, "true");
    boundary.state.health = health({
      status: "capture_failed",
      capture_running: false,
      capture_failed: true,
      capture_failure_reason: "listener died",
    });
    render(<App />);

    const card = within(permissionsCard());
    expect(await card.findByText("capture failed")).toBeInTheDocument();
    expect(card.getByText(/Capture listener stopped/i)).toHaveTextContent("listener died");
  });

  it("warns when capture is running but receiving no events (stalled)", async () => {
    boundary.state.health = health({ capture_stalled: true });
    render(<App />);

    const card = within(permissionsCard());
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_health"));
    expect(await card.findByText(/hasn't received any input events/i)).toBeInTheDocument();
  });

  it("recovers the UI when capture comes up after launch", async () => {
    vi.useFakeTimers();
    try {
      // Wizard acknowledged; capture is down at launch (listener pending).
      window.localStorage.setItem(FIRST_RUN_ACK_KEY, "true");
      boundary.state.health = health({
        capture_running: false,
        permissions: {
          capture_available: true,
          capture_probe_confirmed: false,
          active_window_available: true,
          message: "",
          setup_steps: [],
        },
      });
      render(<App />);
      // Flush the mount health load (state updates must run inside act).
      await act(async () => {
        await vi.advanceTimersByTimeAsync(0);
      });

      const card = within(permissionsCard());
      expect(card.getByText("listener pending")).toBeInTheDocument();

      // Capture comes up after launch; the background health poll should notice.
      boundary.state.health = health({ capture_running: true });
      await act(async () => {
        await vi.advanceTimersByTimeAsync(HEALTH_POLL_MS);
      });

      expect(card.getByText("listener running")).toBeInTheDocument();
    } finally {
      vi.useRealTimers();
    }
  });
});
