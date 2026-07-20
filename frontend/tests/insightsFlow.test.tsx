import { cleanup, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    history: Record<string, unknown>[];
    focusSummary: Record<string, unknown>;
  } = {
    health: {},
    history: [],
    focusSummary: {},
  };

  const invoke = vi.fn(async (cmd: string): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "get_session_history":
        return state.history;
      case "get_focus_summary":
        return state.focusSummary;
      case "get_prediction_history":
      case "get_app_rules":
      case "get_context_timeline":
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

const rawSummary = (id: string, focus: number, deep: number, snap: number) => ({
  record: {
    sessionId: id,
    goal: "g",
    status: "COMPLETED",
    focusMode: "normal",
    startedAt: null,
    endedAt: null,
  },
  recap: {
    sessionId: id,
    goal: "g",
    durationSecs: 0,
    avgFocusScore: focus,
    avgDistractionRisk: 0,
    snapbackCount: snap,
    thrashSpikes: 0,
    deepFocusPct: deep,
  },
});

const insightsCard = () =>
  screen.getByRole("heading", { name: "Insights" }).closest("section") as HTMLElement;

const focusSummaryCard = () =>
  screen.getByRole("heading", { name: "Recent Focus" }).closest("section") as HTMLElement;

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.history = [];
  boundary.state.focusSummary = {};
});

afterEach(() => {
  cleanup();
});

describe("Insights card", () => {
  it("renders tiles and one bar per session from history", async () => {
    boundary.state.history = [rawSummary("a", 60, 40, 2), rawSummary("b", 80, 20, 3)];
    render(<App />);

    await screen.findByRole("heading", { name: "Insights" });
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_session_history", { limit: 20 }));

    const card = insightsCard();
    const insights = within(card);
    // Aggregates: avg focus (60+80)/2 = 70, avg deep (40+20)/2 = 30%.
    expect(insights.getByText("70")).toBeInTheDocument();
    expect(insights.getByText("30%")).toBeInTheDocument();
    // One bar per session.
    expect(card.querySelectorAll("rect.chart-bar")).toHaveLength(2);
  });

  it("shows an empty state when there are no sessions", async () => {
    boundary.state.history = [];
    render(<App />);

    const card = insightsCard();
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_session_history", { limit: 20 }));
    expect(within(card).getByText(/No completed sessions yet/i)).toBeInTheDocument();
    expect(card.querySelectorAll("rect.chart-bar")).toHaveLength(0);
  });
});

describe("Focus summary card", () => {
  it("renders the recent-focus tiles from get_focus_summary", async () => {
    boundary.state.focusSummary = {
      sample_count: 40,
      avg_focus_score: 72.4,
      peak_focus_score: 95,
      distracted_samples: 6,
      distracted_fraction: 0.15,
      longest_focus_streak: 18,
    };
    render(<App />);

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("get_focus_summary", { limit: 200 }),
    );

    const card = focusSummaryCard();
    const tiles = within(card);
    expect(tiles.getByText("72")).toBeInTheDocument();
    expect(tiles.getByText("95")).toBeInTheDocument();
    expect(tiles.getByText("15%")).toBeInTheDocument();
    expect(tiles.getByText("18")).toBeInTheDocument();
  });

  it("shows an empty state when there are no predictions yet", async () => {
    boundary.state.focusSummary = { sample_count: 0 };
    render(<App />);

    const card = focusSummaryCard();
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("get_focus_summary", { limit: 200 }),
    );
    expect(within(card).getByText(/No predictions recorded yet/i)).toBeInTheDocument();
  });
});
