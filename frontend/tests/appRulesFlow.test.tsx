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
  } = {
    activeSession: null,
    analytics: { avg_focus_score: 80, sample_count: 10, productive_session_streak: 2, hourly: [], top_apps: [] },
    health: {},
    rules: [],
    timeline: [],
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
          rule_type: String(request.ruleType ?? "allow"),
          note: request.note ?? null,
          created_at: "2026-07-11T00:00:00Z",
          updated_at: "2026-07-11T00:00:00Z",
        };
        state.rules = [...state.rules, rule];
        return rule;
      }
      case "delete_app_rule": {
        state.rules = state.rules.filter((r) => r.id !== args?.id);
        return null;
      }
      case "get_prediction_history":
        return [];
      case "get_context_timeline":
        return state.timeline;
      case "get_analytics":
        return state.analytics;
      case "get_session_history":
        return [];
      case "get_summary_report":
        return { total_attended_minutes: 0, session_count: 0 };
      case "get_focus_summary":
        return {};
      case "get_settings":
        return { default_focus_mode: "normal" };
      case "get_privacy_settings":
        return { private_mode: false, excluded_apps: [] };
      case "get_recording_status":
        return { state: "recording" };
      case "get_pomodoro_status":
        return { enabled: false };
      case "get_attended_targets":
        return { daily_target_minutes: null, weekly_target_minutes: null };
      case "get_autostart_status":
        return { enabled: false };
      case "get_diagnostics_snapshot":
        return { database_size_bytes: 0, log_size_bytes: 0, model_size_bytes: 0 };
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
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.rules = [];
  boundary.state.timeline = [];
  boundary.state.activeSession = null;
  boundary.state.analytics = { avg_focus_score: 80, sample_count: 10, productive_session_streak: 2, hourly: [], top_apps: [] };
});

afterEach(() => {
  cleanup();
});

describe("App rules add/delete flow", () => {
  it("adds a rule and shows it in the list", async () => {
    renderApp("settings", "focus");
    await screen.findByRole("heading", { name: "Personal App Rules" });

    fireEvent.change(screen.getByPlaceholderText("discord, notion, youtube"), {
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
        rule_type: "block",
        note: null,
        created_at: "2026-07-11T00:00:00Z",
        updated_at: "2026-07-11T00:00:00Z",
      },
    ];
    renderApp("settings", "focus");
    expect(await screen.findByText("notion")).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Remove" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("delete_app_rule", { id: 7 }),
    );
    await waitFor(() => expect(screen.queryByText("notion")).not.toBeInTheDocument());
  });

  it("creates an allow rule with one click from the context timeline", async () => {
    boundary.state.activeSession = {
      session_id: "session-123",
      goal: "Code review",
      status: "ACTIVE",
      focus_mode: "normal",
      started_at: "2026-08-14T00:00:00Z",
    };
    boundary.state.timeline = [
      {
        timestamp: "2026-08-14T00:05:00Z",
        app_name: "Slack",
        window_title: "team-general - Slack",
        summary: "Chatting with team",
      },
    ];

    renderApp("review");
    expect(await screen.findByText("Slack")).toBeInTheDocument();


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
      avg_focus_score: 80,
      sample_count: 10,
      productive_session_streak: 2,
      hourly: [],
      top_apps: [{ app_name: "Steam", window_count: 15 }],
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


