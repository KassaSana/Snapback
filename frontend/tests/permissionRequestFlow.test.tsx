import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Permission state starts ungranted (setup_steps non-empty) so the "Grant access" button
// is present — that button is what raises the real OS dialog on macOS.
const ungranted = {
  capture_available: false,
  capture_probe_confirmed: false,
  active_window_available: false,
  message: "Grant Accessibility permission to Snapback to read foreground context.",
  setup_steps: ["Open System Settings > Privacy & Security > Accessibility."],
};

const granted = {
  capture_available: true,
  capture_probe_confirmed: true,
  active_window_available: true,
  message: "macOS Accessibility permission is available.",
  setup_steps: [],
};

const boundary = vi.hoisted(() => {
  const state = {
    permissions: null as Record<string, unknown> | null,
    grantOnRequest: true,
  };

  const invoke = vi.fn(async (cmd: string) => {
    switch (cmd) {
      case "request_permissions":
        // Model the OS dialog: the user grants, so the follow-up probe flips to granted.
        if (state.grantOnRequest) state.permissions = state.grantedValue;
        return state.permissions;
      case "refresh_permissions":
        return state.permissions;
      case "get_health":
        return {
          status: "online",
          capture_running: false,
          capture_failed: false,
          capture_events_dropped: 0,
          permissions: state.permissions,
          classifier: { backend: "heuristic", onnx_runtime_enabled: false, model_path: null },
        };
      case "get_settings":
        return { default_focus_mode: "normal" };
      case "get_autostart":
        return { enabled: false, supported: false };
      case "get_active_session":
      case "get_latest_prediction":
        return null;
      case "get_focus_summary":
      case "get_training_deploy_status":
        return {};
      case "get_prediction_history":
      case "get_pomodoro_status":
      case "get_app_rules":
      case "get_context_timeline":
        return [];
      default:
        return null;
    }
  });
  const listen = vi.fn(async () => () => {});
  return { invoke, listen, state: state as typeof state & { grantedValue: unknown } };
});

vi.mock("@tauri-apps/api/core", () => ({ invoke: boundary.invoke }));
vi.mock("@tauri-apps/api/event", () => ({ listen: boundary.listen }));

import App from "../src/App";

beforeEach(() => {
  boundary.invoke.mockClear();
  boundary.state.permissions = ungranted;
  boundary.state.grantedValue = granted;
  boundary.state.grantOnRequest = true;
});

afterEach(() => {
  cleanup();
});

describe("permission request flow", () => {
  it("asks the OS for permission when Grant access is clicked", async () => {
    render(<App />);

    // Two render while ungranted — the onboarding wizard (modal, on top) and the
    // Permissions card behind it. Either should reach the same IPC command.
    const grant = await screen.findAllByRole("button", { name: /Grant access/ });
    fireEvent.click(grant[0]);

    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("request_permissions"));
  });

  it("hides Grant access once permission is already held", async () => {
    boundary.state.permissions = granted;
    render(<App />);

    // Wait for the first health poll to settle before asserting an absence.
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_health"));
    expect(screen.queryAllByRole("button", { name: /Grant access/ })).toHaveLength(0);
  });

  it("never prompts from the pollable refresh path", async () => {
    // The dialog-free probe must stay dialog-free: refreshing should never call
    // request_permissions, or a timer-driven refresh would spam the user with dialogs.
    render(<App />);

    const refresh = await screen.findAllByRole("button", { name: /Check again|Refresh permissions/ });
    fireEvent.click(refresh[0]);

    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("refresh_permissions"));
    expect(boundary.invoke).not.toHaveBeenCalledWith("request_permissions");
  });
});
