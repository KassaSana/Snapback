import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Roadmap 10.9. Navigation and focus for the second level inside Settings, plus the two
// behaviours that are allowed to move the user: a deep link, and a real actionable failure.
const boundary = vi.hoisted(() => {
  const state: { health: Record<string, unknown>; settings: Record<string, unknown> } = {
    health: {},
    settings: {},
  };

  const invoke = vi.fn(async (cmd: string): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
      case "get_settings":
        return state.settings;
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

vi.mock("../src/bridge", () => ({ invoke: boundary.invoke, listen: boundary.listen }));

import App from "../src/App";

const healthyCaptureRunning = (): Record<string, unknown> => ({
  status: "online",
  capture_running: true,
  capture_failed: false,
  capture_events_dropped: 0,
  developer_tools_enabled: true,
  permissions: {
    capture_available: true,
    capture_probe_confirmed: true,
    active_window_available: true,
    message: "",
    setup_steps: [],
  },
  classifier: { backend: "heuristic", onnx_runtime_enabled: false, model_path: null },
});

const openSettings = async () => {
  fireEvent.click(await screen.findByRole("tab", { name: "Settings" }));
};

const sectionTab = (name: string) => screen.getByRole("tab", { name });

beforeEach(() => {
  window.localStorage.clear();
  window.location.hash = "";
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.settings = { default_focus_mode: "normal" };
});

afterEach(() => {
  cleanup();
  window.location.hash = "";
});

describe("Settings second-level navigation", () => {
  it("moves between sections with the arrow keys and carries focus", async () => {
    render(<App />);
    await openSettings();

    const general = sectionTab("General");
    general.focus();
    fireEvent.keyDown(general, { key: "ArrowRight" });

    // Selection and focus move together — a tablist that changes the panel but leaves focus
    // behind strands a keyboard user on the tab they just left.
    expect(sectionTab("Focus")).toHaveAttribute("aria-selected", "true");
    expect(sectionTab("Focus")).toHaveFocus();

    fireEvent.keyDown(sectionTab("Focus"), { key: "End" });
    expect(sectionTab("Advanced")).toHaveAttribute("aria-selected", "true");
    expect(sectionTab("Advanced")).toHaveFocus();

    // Wraps rather than dead-ending at the last tab.
    fireEvent.keyDown(sectionTab("Advanced"), { key: "ArrowRight" });
    expect(sectionTab("General")).toHaveAttribute("aria-selected", "true");

    fireEvent.keyDown(sectionTab("General"), { key: "ArrowLeft" });
    expect(sectionTab("Advanced")).toHaveAttribute("aria-selected", "true");

    fireEvent.keyDown(sectionTab("Advanced"), { key: "Home" });
    expect(sectionTab("General")).toHaveAttribute("aria-selected", "true");
  });

  it("keeps exactly one section tab in the tab order and labels its panel", async () => {
    render(<App />);
    await openSettings();

    // Roving tabindex: one stop for the whole group, arrows move within it.
    const tabs = ["General", "Focus", "Privacy & permissions", "Advanced"].map(sectionTab);
    expect(tabs.filter((tab) => tab.getAttribute("tabindex") === "0")).toHaveLength(1);
    expect(sectionTab("General")).toHaveAttribute("tabindex", "0");

    const panel = document.getElementById("settings-panel-general");
    expect(panel).toHaveAttribute("aria-labelledby", "settings-tab-general");
    expect(sectionTab("General")).toHaveAttribute("aria-controls", "settings-panel-general");
  });

  // The item requires support instructions ("open Settings → Privacy") to keep working after
  // the reorganisation. A link that silently landed on General would be worse than no link.
  it("opens the section named by a deep link", async () => {
    window.location.hash = "#settings/privacy";
    render(<App />);
    await openSettings();

    expect(sectionTab("Privacy & permissions")).toHaveAttribute("aria-selected", "true");
    expect(await screen.findByRole("heading", { name: "Permissions" })).toBeInTheDocument();
  });

  it("ignores a hash that does not name a section", async () => {
    window.location.hash = "#settings/nonsense";
    render(<App />);
    await openSettings();

    expect(sectionTab("General")).toHaveAttribute("aria-selected", "true");
  });

  // The one exception to "Advanced stays put": a failure the user can act on from the
  // revealed section.
  it("reveals Privacy & permissions when capture is blocked", async () => {
    boundary.state.health = {
      ...healthyCaptureRunning(),
      capture_running: false,
      capture_failed: true,
      permissions: {
        capture_available: false,
        capture_probe_confirmed: false,
        active_window_available: false,
        message: "Screen recording permission is required.",
        setup_steps: ["Open System Settings"],
      },
    };

    render(<App />);
    await openSettings();

    await waitFor(() =>
      expect(sectionTab("Privacy & permissions")).toHaveAttribute("aria-selected", "true"),
    );

    // And it does not drag the user back after they navigate away, which would make every
    // other section unusable for as long as the failure lasts.
    fireEvent.click(sectionTab("General"));
    await waitFor(() => expect(sectionTab("General")).toHaveAttribute("aria-selected", "true"));
    expect(sectionTab("Privacy & permissions")).toHaveAttribute("aria-selected", "false");
  });

  it("leaves the user on General when nothing is wrong", async () => {
    render(<App />);
    await openSettings();

    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_health"));
    expect(sectionTab("General")).toHaveAttribute("aria-selected", "true");
  });

  // The single badge that replaced the header's permanent classifier/model/quality fields.
  it("replaces the header engineering fields with one badge that opens the details", async () => {
    render(<App />);
    await screen.findByRole("tab", { name: "Settings" });

    // The three fields the item names are gone from the header.
    expect(screen.queryByText("Classifier")).not.toBeInTheDocument();
    expect(screen.queryByText("Heuristic only")).not.toBeInTheDocument();
    expect(screen.queryByText(/ONNX runtime/i)).not.toBeInTheDocument();

    expect(await screen.findByText("All systems normal")).toBeInTheDocument();

    // ...and the detail behind it is one click away, on the right surface and section.
    fireEvent.click(screen.getByRole("button", { name: "Technical details" }));
    expect(screen.getByRole("tab", { name: "Settings" })).toHaveAttribute(
      "aria-selected",
      "true",
    );
    expect(sectionTab("Advanced")).toHaveAttribute("aria-selected", "true");
    expect(await screen.findByRole("heading", { name: "Diagnostics" })).toBeInTheDocument();
  });

  it("sends the badge to Privacy & permissions when capture is the problem", async () => {
    boundary.state.health = {
      ...healthyCaptureRunning(),
      capture_running: false,
      capture_failed: true,
    };

    render(<App />);
    expect(await screen.findByText("Capture stopped")).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Technical details" }));
    expect(sectionTab("Privacy & permissions")).toHaveAttribute("aria-selected", "true");
  });
});
