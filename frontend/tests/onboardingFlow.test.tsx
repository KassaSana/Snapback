import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Roadmap 2.12. The guided continuation, driven against the real App so that "the step
// advances from app state" is asserted through the state actually changing.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    settings: Record<string, unknown>;
    privacy: Record<string, unknown>;
    prediction: Record<string, unknown> | null;
  } = {
    health: {},
    settings: {},
    privacy: {},
    prediction: null,
  };

  const session = (overrides: Record<string, unknown> = {}) => ({
    session_id: "sess-1",
    goal: "Write tests",
    status: "ACTIVE",
    focus_mode: "normal",
    started_at: "2026-07-11T00:00:00Z",
    ended_at: null,
    ...overrides,
  });

  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
      case "get_settings":
        return state.settings;
      case "get_privacy_settings":
        return state.privacy;
      case "get_latest_prediction":
        return state.prediction;
      case "start_session":
        return session({ goal: String(args?.goal ?? "") });
      case "stop_session":
        return session({ status: "COMPLETED", ended_at: "2026-07-11T00:30:00Z" });
      case "get_session_recap":
        return { session_id: "sess-1", goal: "Write tests", duration_secs: 1800 };
      case "submit_label":
        return true;
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
  developer_tools_enabled: false,
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
  window.location.hash = "";
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.settings = { default_focus_mode: "normal" };
  boundary.state.privacy = { private_mode: false, excluded_apps: [] };
  boundary.state.prediction = null;
});

afterEach(() => cleanup());

const guide = () =>
  screen.getByRole("region", { name: "Getting started" }) as HTMLElement;
const queryGuide = () => screen.queryByRole("region", { name: "Getting started" });
const goalField = () => screen.getByPlaceholderText("Ship the snapback overlay");
const commandCalls = (name: string) =>
  boundary.invoke.mock.calls.filter(([cmd]) => cmd === name).length;

describe("onboarding continuation", () => {
  it("starts at the goal step once capture is working", async () => {
    render(<App />);

    await waitFor(() => expect(queryGuide()).not.toBeNull());
    expect(within(guide()).getByText("Step 1 of 6")).toBeInTheDocument();
    expect(within(guide()).getByText("Name what you're working on")).toBeInTheDocument();
  });

  it("stays away until capture actually works", async () => {
    boundary.state.health = {
      ...healthyCaptureRunning(),
      capture_running: false,
      permissions: {
        capture_available: false,
        capture_probe_confirmed: false,
        active_window_available: false,
        message: "Screen recording permission is required.",
        setup_steps: ["Open System Settings"],
      },
    };

    render(<App />);
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_health"));
    // 1.1's wizard owns this state; a walkthrough of reading verdicts over it would be noise.
    expect(queryGuide()).toBeNull();
  });

  // THE RULE. There is no Next button anywhere in the guide, and the step still moves.
  it("advances from app state rather than from a click", async () => {
    render(<App />);
    await waitFor(() => expect(queryGuide()).not.toBeNull());

    expect(within(guide()).queryByRole("button", { name: /next/i })).toBeNull();

    fireEvent.change(goalField(), { target: { value: "Write tests" } });
    await waitFor(() => expect(within(guide()).getByText("Step 2 of 6")).toBeInTheDocument());
    expect(within(guide()).getByText("Start the session")).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await waitFor(() => expect(within(guide()).getByText("Step 3 of 6")).toBeInTheDocument());
    expect(within(guide()).getByText("Wait for your first reading")).toBeInTheDocument();
  });

  // The guide must never manufacture the records it is describing.
  it("issues no commands of its own", async () => {
    render(<App />);
    await waitFor(() => expect(queryGuide()).not.toBeNull());

    // Everything the guide offers, clicked — none of it may touch the session or label API.
    fireEvent.click(within(guide()).getByRole("button", { name: "Skip the walkthrough" }));

    expect(commandCalls("start_session")).toBe(0);
    expect(commandCalls("stop_session")).toBe(0);
    expect(commandCalls("submit_label")).toBe(0);
  });

  it("meets a user who ran ahead at the step they actually reached", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });

    // Start a session without ever consulting the guide.
    fireEvent.change(goalField(), { target: { value: "Write tests" } });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));

    // It must not walk them back to "type a goal" over a running session.
    await waitFor(() => expect(within(guide()).getByText("Step 3 of 6")).toBeInTheDocument());
    expect(within(guide()).queryByText("Name what you're working on")).toBeNull();
  });

  it("skips durably and can be replayed from Settings", async () => {
    render(<App />);
    await waitFor(() => expect(queryGuide()).not.toBeNull());

    fireEvent.click(within(guide()).getByRole("button", { name: "Skip the walkthrough" }));
    expect(queryGuide()).toBeNull();

    // Survives a relaunch, which is the difference between skipping and dismissing.
    cleanup();
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });
    expect(queryGuide()).toBeNull();

    // Resumable from the Settings entry point.
    fireEvent.click(screen.getByRole("tab", { name: "Settings" }));
    fireEvent.click(await screen.findByRole("button", { name: "Replay the walkthrough" }));

    await waitFor(() => expect(queryGuide()).not.toBeNull());
    expect(within(guide()).getByText("Step 1 of 6")).toBeInTheDocument();
    // Replaying created nothing.
    expect(commandCalls("start_session")).toBe(0);
    expect(commandCalls("submit_label")).toBe(0);
  });

  // Failures hand off to the surface that owns them instead of growing a second recovery UI.
  it("routes a capture failure to the permissions section", async () => {
    boundary.state.health = { ...healthyCaptureRunning(), capture_failed: true };

    render(<App />);
    await waitFor(() => expect(queryGuide()).not.toBeNull());

    const alert = within(guide()).getByRole("alert");
    expect(alert).toHaveTextContent("Capture stopped");

    fireEvent.click(within(alert).getByRole("button", { name: "Open permissions" }));
    expect(screen.getByRole("tab", { name: "Settings" })).toHaveAttribute(
      "aria-selected",
      "true",
    );
    expect(screen.getByRole("tab", { name: "Privacy & permissions" })).toHaveAttribute(
      "aria-selected",
      "true",
    );
  });

  it("says so when private mode is why nothing is being recorded", async () => {
    boundary.state.privacy = { private_mode: true, excluded_apps: [] };

    render(<App />);
    await waitFor(() => expect(queryGuide()).not.toBeNull());

    await waitFor(() =>
      expect(within(guide()).getByRole("alert")).toHaveTextContent("Private mode is on"),
    );
  });

  it("finishes for good once the recap has been read", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });

    fireEvent.change(goalField(), { target: { value: "Write tests" } });
    fireEvent.click(screen.getByRole("button", { name: "Start session" }));
    await screen.findByText("running");

    fireEvent.click(await screen.findByRole("button", { name: "Stop session" }));
    await waitFor(() => expect(within(guide()).getByText("Step 6 of 6")).toBeInTheDocument());
    expect(within(guide()).getByText("Read the recap in Review")).toBeInTheDocument();

    // Reading it is the last step, and it completes by being read.
    fireEvent.click(screen.getByRole("tab", { name: "Review" }));
    await waitFor(() => expect(queryGuide()).toBeNull());

    // Going back to Now does not un-finish it, and neither does a relaunch.
    fireEvent.click(screen.getByRole("tab", { name: "Now" }));
    expect(queryGuide()).toBeNull();

    cleanup();
    render(<App />);
    await screen.findByRole("heading", { name: "Session Control" });
    expect(queryGuide()).toBeNull();
  });
});
