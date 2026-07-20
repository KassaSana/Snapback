import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const boundary = vi.hoisted(() => {
  const state = {
    privacy: { private_mode: false, excluded_apps: [], local_only: true } as Record<string, unknown>,
    health: {
      status: "online",
      capture_running: true,
      capture_failed: false,
      capture_events_dropped: 0,
      permissions: { capture_available: true, capture_probe_confirmed: true, active_window_available: true, message: "", setup_steps: [] },
      classifier: { backend: "heuristic", onnx_runtime_enabled: false, model_path: null },
    },
  };
  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>) => {
    switch (cmd) {
      case "get_privacy_settings": return state.privacy;
      case "set_private_mode":
        state.privacy = { ...state.privacy, private_mode: Boolean(args?.enabled) };
        return state.privacy;
      case "set_privacy_exclusions":
        state.privacy = { ...state.privacy, excluded_apps: args?.excludedApps ?? [] };
        return state.privacy;
      case "get_health": return state.health;
      case "get_settings": return { default_focus_mode: "normal" };
      case "get_active_session": case "get_latest_prediction": return null;
      case "get_prediction_history": case "get_app_rules": case "get_context_timeline": case "get_pomodoro_status": return [];
      case "get_focus_summary": return {};
      case "get_training_deploy_status": return {};
      default: return null;
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
  boundary.state.privacy = { private_mode: false, excluded_apps: [], local_only: true };
});

afterEach(() => cleanup());

describe("privacy controls", () => {
  it("toggles private mode through the backend", async () => {
    render(<App />);
    const toggle = await screen.findByRole("checkbox", { name: "Private mode" });
    await waitFor(() => expect(toggle).not.toBeDisabled());
    fireEvent.click(toggle);
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("set_private_mode", { enabled: true }));
    expect(toggle).toBeChecked();
  });

  it("adds and removes an excluded app", async () => {
    render(<App />);
    const input = await screen.findByPlaceholderText("Banking, 1Password");
    fireEvent.change(input, { target: { value: "Banking" } });
    fireEvent.click(screen.getByRole("button", { name: "Add exclusion" }));
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("set_privacy_exclusions", { excludedApps: ["Banking"] }));
    expect(await screen.findByText("Banking")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Remove" }));
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("set_privacy_exclusions", { excludedApps: [] }));
  });
});
