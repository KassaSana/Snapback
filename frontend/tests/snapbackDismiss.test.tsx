import { act, cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { FIRST_RUN_ACK_KEY } from "../src/permissionWizardState";

// Mock the native boundary so the real api.ts + useLiveData run end to end. Unlike the
// other flow tests, this one needs to actually fire a subscribed event handler (the
// "snapback" listener), so the listen mock records handlers instead of being a no-op.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    restore: () => Promise<{ ok: boolean; message: string }>;
  } = {
    health: {},
    restore: async () => ({ ok: true, message: "Window activated successfully" }),
  };
  const listeners: Record<string, Array<(event: { payload: unknown }) => void>> = {};

  const invoke = vi.fn(async (cmd: string): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
      case "dismiss_snapback":
        return null;
      case "restore_snapback_target":
        return state.restore();
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

beforeEach(() => {
  window.localStorage.clear();
  window.localStorage.setItem(FIRST_RUN_ACK_KEY, "true");
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.restore = async () => ({ ok: true, message: "Window activated successfully" });
});

afterEach(() => {
  cleanup();
});

describe("Snapback note dismiss and restore", () => {
  it("shows the note on a snapback event and clears it via dismiss_snapback", async () => {
    render(<App />);
    // ADR-0003 replaced the "Live Prediction" card with the state-first hero on Now.
    await screen.findByRole("heading", { name: "Focus state" });

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

  it("activates target window and clears note via restore_snapback_target", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Focus state" });

    act(() => {
      boundary.emit("snapback", { summary: "Return to auth.ts" });
    });

    expect(await screen.findByText(/Snapback: Return to auth\.ts/)).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Take me back" }));

    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("restore_snapback_target"));
    expect(screen.queryByText(/Snapback: Return to auth\.ts/)).not.toBeInTheDocument();
  });

  it("keeps the note, shows the reason, and allows a retry when the restore fails", async () => {
    // The handler used to clear the note before the command ran and ignore what came back,
    // so a failed activation was indistinguishable from a successful one and took the retry
    // button with it. The native side keeps its target across a failure, so the second click
    // here is a real retry against the same window.
    boundary.state.restore = async () => ({
      ok: false,
      message: "Could not find a window matching the target",
    });
    render(<App />);
    await screen.findByRole("heading", { name: "Focus state" });

    act(() => {
      boundary.emit("snapback", { summary: "Return to auth.ts" });
    });
    expect(await screen.findByText(/Snapback: Return to auth\.ts/)).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Take me back" }));

    expect(
      await screen.findByText(/couldn't bring it back \(Could not find a window matching the target\)/),
    ).toBeInTheDocument();
    // The original summary survives in the rewritten note, and both actions stay available.
    expect(screen.getByText(/Snapback: Return to auth\.ts/)).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Take me back" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Dismiss" })).toBeInTheDocument();

    // A second failure rewrites the note rather than stacking a second reason onto it.
    fireEvent.click(screen.getByRole("button", { name: "Take me back" }));
    await waitFor(() =>
      expect(
        boundary.invoke.mock.calls.filter(([cmd]) => cmd === "restore_snapback_target"),
      ).toHaveLength(2),
    );
    expect(screen.getAllByText(/couldn't bring it back/)).toHaveLength(1);

    // And the retry succeeding clears the note the way a first-try success does.
    boundary.state.restore = async () => ({ ok: true, message: "Window activated successfully" });
    fireEvent.click(screen.getByRole("button", { name: "Take me back" }));
    await waitFor(() =>
      expect(screen.queryByText(/Snapback: Return to auth\.ts/)).not.toBeInTheDocument(),
    );
  });

  it("treats a thrown IPC error like a failed restore instead of swallowing it", async () => {
    boundary.state.restore = async () => {
      throw new Error("ipc unavailable");
    };
    render(<App />);
    await screen.findByRole("heading", { name: "Focus state" });

    act(() => {
      boundary.emit("snapback", { summary: "Return to auth.ts" });
    });
    expect(await screen.findByText(/Snapback: Return to auth\.ts/)).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Take me back" }));

    expect(await screen.findByText(/couldn't bring it back \(ipc unavailable\)/)).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Take me back" })).toBeInTheDocument();
  });
});

