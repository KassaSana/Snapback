import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { FIRST_RUN_ACK_KEY } from "../src/permissionWizardState";

// Mock the native boundary (invoke/listen) so the real api.ts + hooks run end to
// end without a backend. Command responses are mutable per test.
const boundary = vi.hoisted(() => {
  const state: { health: Record<string, unknown> } = { health: {} };

  const invoke = vi.fn(async (cmd: string): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
      case "get_settings":
        return { default_focus_mode: "normal" };
      case "get_privacy_settings":
        return { private_mode: false, excluded_apps: [], local_only: true };
      case "get_diagnostics":
        return { health: state.health, recent_logs: ["ready"] };
      case "get_goal_categories":
        return [{ name: "coding", keywords: ["code", "test"] }];
      case "get_summary_report":
        return { window: "day" };
      case "get_prediction_history":
      case "get_app_rules":
      case "get_context_timeline":
        return [];
      case "get_training_deploy_status":
        return {};
      case "get_latest_prediction":
      case "get_active_session":
        return null;
      default:
        return null;
    }
  });

  // Every event subscription resolves to a no-op unlisten.
  const listen = vi.fn(async () => () => {});

  return { state, invoke, listen };
});

vi.mock("../src/bridge", () => ({ invoke: boundary.invoke, listen: boundary.listen }));

// Imported after the mocks are registered.
import App from "../src/App";
import { renderApp } from "./renderApp";

const health = (overrides: Record<string, unknown> = {}): Record<string, unknown> => ({
  status: "online",
  capture_running: false,
  capture_failed: false,
  capture_events_dropped: 0,
  permissions: {
    capture_available: false,
    capture_probe_confirmed: false,
    active_window_available: false,
    message: "Grant Input Monitoring.",
    setup_steps: ["Open Settings", "Enable Snapback"],
  },
  classifier: { backend: "heuristic", onnx_runtime_enabled: false, model_path: null },
  ...overrides,
});

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = health();
});

afterEach(() => {
  cleanup();
});

describe("App first-run permission wizard", () => {
  it("renders the app shell on the Now surface by default", async () => {
    render(<App />);

    // ADR-0003: Now is the default surface, and it holds the session, not the config.
    expect(await screen.findByRole("heading", { name: "Session Control" })).toBeInTheDocument();
    expect(screen.getByRole("tab", { name: "Now" })).toHaveAttribute("aria-selected", "true");
    expect(screen.getByRole("heading", { name: "What are you working on?" })).toBeInTheDocument();
    expect(screen.queryByRole("heading", { name: "Pomodoro" })).not.toBeInTheDocument();
    expect(screen.queryByRole("heading", { name: "Attended time" })).not.toBeInTheDocument();

    // Settings-surface cards must NOT be mounted — that separation is the whole point.
    expect(screen.queryByRole("heading", { name: "Permissions" })).not.toBeInTheDocument();
    expect(screen.queryByRole("heading", { name: "Diagnostics" })).not.toBeInTheDocument();
  });

  // This file's default health has capture *blocked*, which 10.9 treats as an actionable
  // failure and answers by revealing Privacy & permissions. That is correct behaviour and it
  // is asserted in settingsNavFlow; here it would race the explicit navigation these two cases
  // are about. So they opt into a healthy capture and test one thing each.
  const healthyCapture = (): Record<string, unknown> =>
    health({
      capture_running: true,
      permissions: {
        capture_available: true,
        capture_probe_confirmed: true,
        active_window_available: true,
        message: "",
        setup_steps: [],
      },
    });

  // Roadmap 10.9. Settings is four groups, and each card lives in exactly one of them. The
  // negative half is the point: before this item every one of these was on screen at once,
  // which is what made Settings read as an engineering console.
  it("groups the configuration cards into Settings sections", async () => {
    boundary.state.health = healthyCapture();

    const { unmount } = renderApp("settings", "privacy");
    expect(await screen.findByRole("heading", { name: "Permissions" })).toBeInTheDocument();
    expect(screen.queryByRole("heading", { name: "Diagnostics" })).not.toBeInTheDocument();
    expect(screen.queryByDisplayValue("coding")).not.toBeInTheDocument();
    unmount();

    const focusRender = renderApp("settings", "focus");
    expect(await screen.findByDisplayValue("coding")).toBeInTheDocument();
    expect(screen.queryByRole("heading", { name: "Permissions" })).not.toBeInTheDocument();
    focusRender.unmount();

    renderApp("settings", "advanced");
    expect(await screen.findByRole("heading", { name: "Diagnostics" })).toBeInTheDocument();
    expect(screen.queryByRole("heading", { name: "Permissions" })).not.toBeInTheDocument();
  });

  // Settings opens on ordinary settings, never on developer tooling — the complaint the item
  // was opened for.
  it("opens Settings on General rather than on model training", async () => {
    boundary.state.health = healthyCapture();

    renderApp("settings");

    expect(await screen.findByRole("tab", { name: "General" })).toHaveAttribute(
      "aria-selected",
      "true",
    );
    expect(screen.queryByText(/Model training/i)).not.toBeInTheDocument();
    expect(screen.queryByRole("heading", { name: "Diagnostics" })).not.toBeInTheDocument();
  });

  it("moves between surfaces with the arrow keys", async () => {
    render(<App />);

    const nowTab = await screen.findByRole("tab", { name: "Now" });
    nowTab.focus();
    fireEvent.keyDown(nowTab, { key: "ArrowRight" });

    expect(screen.getByRole("tab", { name: "Review" })).toHaveAttribute("aria-selected", "true");
    // The panel is labelled by the selected tab, so screen readers announce the change.
    expect(screen.getByRole("tabpanel")).toHaveAttribute(
      "aria-labelledby",
      "surface-tab-review",
    );
  });

  it("shows the wizard on first run when capture isn't ready", async () => {
    render(<App />);

    const dialog = await screen.findByRole("dialog");
    const wizard = within(dialog);
    expect(wizard.getByText(/Welcome to Snapback/i)).toBeInTheDocument();
    // Platform setup steps from the health probe are surfaced inside the wizard.
    expect(wizard.getByText("Enable Snapback")).toBeInTheDocument();
    expect(wizard.getByRole("button", { name: /Check again/i })).toBeInTheDocument();
  });

  it("lets the user pick a default focus mode from the wizard", async () => {
    render(<App />);

    const dialog = await screen.findByRole("dialog");
    const wizard = within(dialog);
    const select = wizard.getByLabelText("Default focus mode") as HTMLSelectElement;
    await waitFor(() => expect(select.value).toBe("normal"));

    fireEvent.change(select, { target: { value: "deep" } });

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("set_focus_mode", { mode: "deep" }),
    );
    expect(select.value).toBe("deep");
  });

  it("hides the wizard once capture is confirmed running", async () => {
    boundary.state.health = health({
      capture_running: true,
      permissions: {
        capture_available: true,
        capture_probe_confirmed: true,
        active_window_available: true,
        message: "",
        setup_steps: [],
      },
    });
    render(<App />);

    // Wait for the Now surface to settle, then assert the wizard never appears. (Was
    // waiting on the Permissions heading, which ADR-0003 moved to the Settings surface —
    // the wait was incidental to this test, not the thing under assertion.)
    await screen.findByRole("heading", { name: "Session Control" });
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_health"));
    expect(screen.queryByRole("dialog")).not.toBeInTheDocument();
  });

  it("does not show the wizard when first run is already acknowledged", async () => {
    window.localStorage.setItem(FIRST_RUN_ACK_KEY, "true");
    render(<App />);

    await screen.findByRole("heading", { name: "Session Control" });
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_health"));
    expect(screen.queryByRole("dialog")).not.toBeInTheDocument();
  });
});
