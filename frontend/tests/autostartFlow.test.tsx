import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const boundary = vi.hoisted(() => {
  const state = {
    autostart: { enabled: false, supported: true },
    health: {
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
    },
  };

  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>) => {
    switch (cmd) {
      case "get_autostart":
        return state.autostart;
      case "set_autostart":
        state.autostart = { enabled: Boolean(args?.enabled), supported: true };
        return state.autostart;
      case "get_health":
        return state.health;
      case "get_settings":
        return { default_focus_mode: "normal" };
      case "get_active_session":
      case "get_latest_prediction":
        return null;
      case "get_prediction_history":
      case "get_focus_summary":
      case "get_pomodoro_status":
      case "get_app_rules":
      case "get_context_timeline":
        return cmd === "get_focus_summary" ? {} : [];
      case "get_training_deploy_status":
        return {};
      default:
        return null;
    }
  });
  const listen = vi.fn(async () => () => {});
  return { invoke, listen, state };
});

vi.mock("@tauri-apps/api/core", () => ({ invoke: boundary.invoke }));
vi.mock("@tauri-apps/api/event", () => ({ listen: boundary.listen }));

import App from "../src/App";

beforeEach(() => {
  boundary.invoke.mockClear();
  boundary.state.autostart = { enabled: false, supported: true };
});

afterEach(() => {
  cleanup();
});

describe("autostart settings", () => {
  it("loads and toggles the Windows start-on-login setting", async () => {
    render(<App />);

    const toggle = await screen.findByRole("checkbox", { name: /Start on login/ });
    await waitFor(() => expect(toggle).not.toBeDisabled());

    fireEvent.click(toggle);

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("set_autostart", { enabled: true }),
    );
    expect(toggle).toBeChecked();
  });

  it("disables the toggle when the platform has no backend", async () => {
    boundary.state.autostart = { enabled: false, supported: false };
    render(<App />);

    const toggle = await screen.findByRole("checkbox", { name: /Start on login/ });
    await waitFor(() => expect(toggle).toBeDisabled());
    expect(boundary.invoke).not.toHaveBeenCalledWith("set_autostart", expect.anything());
  });
});
