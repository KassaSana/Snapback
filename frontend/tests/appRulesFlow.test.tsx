import { cleanup, fireEvent, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Mock the native boundary with a stateful rules store so the list round-trips
// through the real api.ts + useAppRules (add pushes, delete removes, get reads).
const boundary = vi.hoisted(() => {
  const state: {
    activeSession: Record<string, unknown> | null;
    analytics: Record<string, unknown>;
    health: Record<string, unknown>;
    rules: Record<string, unknown>[];
    timeline: Record<string, unknown>[];
    history: Record<string, unknown>[];
  } = {
    activeSession: null,
    analytics: { avgFocusScore: 80, sampleCount: 10, productiveSessionStreak: 2, hourly: [], topApps: [] },
    health: {},
    rules: [],
    timeline: [],
    history: [],
  };
  let nextId = 1;

  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "get_active_session":
        return state.activeSession;
      case "get_app_rules":
        return state.rules;
      case "upsert_app_rule": {
        const request = (args?.request ?? {}) as Record<string, unknown>;
        const rule = {
          id: nextId++,
          pattern: String(request.pattern ?? ""),
          ruleType: String(request.ruleType ?? "allow"),
          note: request.note ?? null,
          createdAtMs: Date.parse("2026-07-11T00:00:00Z"),
          updatedAtMs: Date.parse("2026-07-11T00:00:00Z"),
        };
        state.rules = [...state.rules, rule];
        return rule;
      }
      case "delete_app_rule": {
        state.rules = state.rules.filter((r) => r.id !== args?.id);
        return null;
      }
      case "get_prediction_history":
      case "get_session_focus_curve":
        return [];
      case "get_session_longest_snapback":
        return null;
      case "get_context_timeline":
        return state.timeline;
      case "get_analytics":
        return state.analytics;
      case "get_session_history":
        return state.history;
      case "get_summary_report":
        return { sessionCount: 0 };
      case "get_focus_summary":
        return {};
      case "get_settings":
        return { defaultFocusMode: "normal" };
      case "get_privacy_settings":
        return { privateMode: false, excludedApps: [] };
      case "get_recording_status":
        return { state: "recording" };
      case "get_pomodoro_status":
        return { enabled: false };
      case "get_goal_categories":
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

import { renderApp } from "./renderApp";

const healthyCaptureRunning = (): Record<string, unknown> => ({
  status: "online",
  captureRunning: true,
  captureFailed: false,
  captureEventsDropped: 0,
  permissions: {
    captureAvailable: true,
    captureProbeConfirmed: true,
    activeWindowAvailable: true,
    message: "",
    setupSteps: [],
  },
  classifier: { backend: "heuristic", onnxRuntimeEnabled: false, modelPath: null },
});

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.rules = [];
  boundary.state.timeline = [];
  boundary.state.history = [];
  boundary.state.activeSession = null;
  boundary.state.analytics = { avgFocusScore: 80, sampleCount: 10, productiveSessionStreak: 2, hourly: [], topApps: [] };
});

afterEach(() => {
  cleanup();
});

describe("App rules add/delete flow", () => {
  it("adds a rule and shows it in the list", async () => {
    renderApp("settings", "focus");
    await screen.findByRole("heading", { name: "Personal App Rules" });

    fireEvent.change(screen.getByPlaceholderText("YouTube"), {
      target: { value: "discord" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Save rule" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("upsert_app_rule", {
        request: { pattern: "discord", ruleType: "allow", note: null },
      }),
    );
    expect(await screen.findByText("discord")).toBeInTheDocument();
    expect(await screen.findByText(/Saved allow rule/i)).toBeInTheDocument();
  });

  it("does not save when the pattern is empty", async () => {
    renderApp("settings", "focus");
    await screen.findByRole("heading", { name: "Personal App Rules" });

    fireEvent.click(screen.getByRole("button", { name: "Save rule" }));

    expect(await screen.findByText(/Enter an app name or keyword/i)).toBeInTheDocument();
    expect(boundary.invoke).not.toHaveBeenCalledWith("upsert_app_rule", expect.anything());
  });

  it("removes a rule from the list", async () => {
    boundary.state.rules = [
      {
        id: 7,
        pattern: "notion",
        ruleType: "block",
        note: null,
        createdAtMs: Date.parse("2026-07-11T00:00:00Z"),
        updatedAtMs: Date.parse("2026-07-11T00:00:00Z"),
      },
    ];
    renderApp("settings", "focus");
    expect(await screen.findByText("notion")).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Remove notion rule" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("delete_app_rule", { id: 7 }),
    );
    await waitFor(() => expect(screen.queryByText("notion")).not.toBeInTheDocument());
  });

  it("creates an allow rule with one click from the context timeline", async () => {
    const session = {
      sessionId: "session-123",
      goal: "Code review",
      status: "COMPLETED",
      focusMode: "normal",
      startedAtMs: Date.parse("2026-08-14T00:00:00Z"),
      endedAtMs: Date.parse("2026-08-14T01:00:00Z"),
    };
    boundary.state.history = [{ record: session, recap: { sessionId: "session-123", goal: "Code review" } }];
    boundary.state.timeline = [
      {
        timestampMs: Date.parse("2026-08-14T00:05:00Z"),
        appName: "Slack",
        windowTitle: "team-general - Slack",
        summary: "Chatting with team",
      },
    ];

    renderApp("review");
    expect(await screen.findByRole("button", { name: "+ Allow" })).toBeInTheDocument();


    const allowBtn = screen.getByRole("button", { name: "+ Allow" });
    fireEvent.click(allowBtn);

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("upsert_app_rule", {
        request: {
          pattern: "Slack",
          ruleType: "allow",
          note: "Created from timeline for Slack",
        },
      }),
    );
  });

  it("creates a block rule with one click from top apps in review", async () => {
    boundary.state.analytics = {
      avgFocusScore: 80,
      sampleCount: 10,
      productiveSessionStreak: 2,
      hourly: [],
      topApps: [{ appName: "Steam", windowCount: 15 }],
    };

    renderApp("review");
    expect(await screen.findByText("Steam")).toBeInTheDocument();

    const blockBtn = screen.getByRole("button", { name: "+ Block" });
    fireEvent.click(blockBtn);

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("upsert_app_rule", {
        request: {
          pattern: "Steam",
          ruleType: "block",
          note: "Created from timeline for Steam",
        },
      }),
    );
  });
});


