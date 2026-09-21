import { cleanup, fireEvent, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    history: Record<string, unknown>[];
    focusSummary: Record<string, unknown>;
    analytics: Record<string, unknown>;
    summary: Record<string, unknown>;
    deleteThrows: boolean;
    reflectionThrows: boolean;
    historyThrows: boolean;
    /** Windows whose analytics load rejects (slice 5: failed range change). */
    failWindows: Set<string>;
  } = {
    health: {},
    history: [],
    focusSummary: {},
    analytics: {},
    summary: {},
    deleteThrows: false,
    reflectionThrows: false,
    historyThrows: false,
    failWindows: new Set(),
  };

  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "get_session_history":
        if (state.historyThrows) throw new Error("history unavailable");
        return state.history;
      // Mirrors the native contract: delete the row if it is there, and report whether one
      // was actually removed (Storage::delete_session → AppState::delete_session).
      case "delete_session": {
        if (state.deleteThrows) throw new Error("delete failed");
        const before = state.history.length;
        state.history = state.history.filter(
          (entry) => (entry.record as { sessionId: string }).sessionId !== args?.sessionId,
        );
        return state.history.length !== before;
      }
      case "save_session_reflection": {
        if (state.reflectionThrows) throw new Error("reflection failed");
        const row = state.history.find(
          (entry) => (entry.record as { sessionId: string }).sessionId === args?.sessionId,
        );
        if (!row) throw new Error("missing session");
        row.record = {
          ...(row.record as object),
          reflection_done: args?.done ?? null,
          reflection_next_step: args?.nextStep ?? null,
        };
        return row.record;
      }
      case "get_focus_summary":
        return state.focusSummary;
      case "get_analytics":
        if (state.failWindows.has(String(args?.window ?? ""))) throw new Error("range failed");
        return state.analytics;
      case "get_summary_report":
        return state.summary;
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

const rawSummary = (id: string, focus: number, deep: number, snap: number) => ({
  record: {
    sessionId: id,
    goal: "g",
    status: "COMPLETED",
    focusMode: "normal",
    startedAtMs: null,
    endedAtMs: null,
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
  screen.getByRole("heading", { name: "Focus per session" }).closest("section") as HTMLElement;

const focusSummaryCard = () =>
  screen.getByRole("heading", { name: "Summary" }).closest("section") as HTMLElement;

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.history = [];
  boundary.state.focusSummary = {};
  boundary.state.analytics = {};
  boundary.state.summary = {};
  boundary.state.deleteThrows = false;
  boundary.state.reflectionThrows = false;
  boundary.state.historyThrows = false;
  boundary.state.failWindows = new Set();
});

afterEach(() => {
  cleanup();
});

describe("Insights card", () => {
  it("renders one bar per session and a concise session aggregate", async () => {
    boundary.state.history = [rawSummary("a", 60, 40, 2), rawSummary("b", 80, 20, 3)];
    renderApp("review");

    await screen.findByRole("heading", { name: "Focus per session" });
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("get_session_history", { limit: 20 }),
    );

    const card = insightsCard();
    const insights = within(card);
    expect(insights.getByText(/30% deep focus.*5 snapbacks/)).toBeInTheDocument();
    expect(screen.getAllByText("Session management")).toHaveLength(1);
    const manager = screen.getByText("Session management").closest("details");
    expect(manager).not.toHaveAttribute("open");
    expect(manager?.querySelectorAll("li")).toHaveLength(2);
    // One bar per session.
    expect(card.querySelectorAll("rect.chart-bar")).toHaveLength(2);
  });

  it("shows an empty state when there are no sessions", async () => {
    boundary.state.history = [];
    renderApp("review");

    const card = insightsCard();
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("get_session_history", { limit: 20 }),
    );
    expect(within(card).getByText(/No completed sessions yet/i)).toBeInTheDocument();
    expect(card.querySelectorAll("rect.chart-bar")).toHaveLength(0);
  });
});

// Roadmap 7.6. The native `delete_session` command has been tested since 2026-07-29; what was
// missing was any way for a user to reach it. These tests are about the reachable path.
describe("Session deletion from Insights", () => {
  const goalSummary = (id: string, goal: string) => {
    const base = rawSummary(id, 50, 0, 0);
    return { record: { ...base.record, goal }, recap: { ...base.recap, goal } };
  };

  const loadTwoSessions = async () => {
    boundary.state.history = [goalSummary("a", "alpha"), goalSummary("b", "bravo")];
    renderApp("review");
    await screen.findByRole("heading", { name: "Focus per session" });
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("get_session_history", { limit: 20 }),
    );
    fireEvent.click(screen.getByText("Session management"));
  };

  it("names each session on its delete button rather than a bare 'Delete'", async () => {
    await loadTwoSessions();
    expect(screen.getByRole("button", { name: "Delete session alpha" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Delete session bravo" })).toBeInTheDocument();
  });

  it("does not delete on the first click, then deletes only the confirmed session", async () => {
    await loadTwoSessions();

    fireEvent.click(screen.getByRole("button", { name: "Delete session alpha" }));
    expect(boundary.invoke).not.toHaveBeenCalledWith("delete_session", expect.anything());

    fireEvent.click(screen.getByRole("button", { name: "Confirm delete session alpha" }));
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("delete_session", { sessionId: "a" }),
    );

    await waitFor(() => expect(screen.getByText("Session deleted.")).toBeInTheDocument());
    expect(screen.queryByRole("button", { name: "Delete session alpha" })).toBeNull();
    expect(screen.getByRole("button", { name: "Delete session bravo" })).toBeInTheDocument();
  });

  it("cancelling leaves the session and never reaches the backend", async () => {
    await loadTwoSessions();

    fireEvent.click(screen.getByRole("button", { name: "Delete session alpha" }));
    fireEvent.click(screen.getByRole("button", { name: "Cancel" }));

    expect(boundary.invoke).not.toHaveBeenCalledWith("delete_session", expect.anything());
    expect(screen.getByRole("button", { name: "Delete session alpha" })).toBeInTheDocument();
  });

  // The native command returns false when no row matched. Saying "deleted" there would claim
  // work SQLite did not do, so the wording has to differ even though the row goes either way.
  it("reports a stale row honestly instead of claiming a delete", async () => {
    await loadTwoSessions();
    // Something else removed it between the list load and the click.
    boundary.state.history = [goalSummary("b", "bravo")];

    fireEvent.click(screen.getByRole("button", { name: "Delete session alpha" }));
    fireEvent.click(screen.getByRole("button", { name: "Confirm delete session alpha" }));

    await waitFor(() =>
      expect(screen.getByText("That session was already gone.")).toBeInTheDocument(),
    );
    expect(screen.queryByText("Session deleted.")).toBeNull();
    expect(screen.queryByRole("button", { name: "Delete session alpha" })).toBeNull();
  });

  // The delete is authoritative once it returns, so a failed refetch must not resurrect the
  // row on screen. `refreshInsights` keeps its last good data by design, which is exactly the
  // trap: without the local prune, this test shows a session the database no longer has.
  it("keeps the row gone even when the follow-up refresh fails", async () => {
    await loadTwoSessions();
    boundary.state.historyThrows = true;

    fireEvent.click(screen.getByRole("button", { name: "Delete session alpha" }));
    fireEvent.click(screen.getByRole("button", { name: "Confirm delete session alpha" }));

    await waitFor(() =>
      expect(screen.queryByRole("button", { name: "Delete session alpha" })).toBeNull(),
    );
    expect(screen.getByRole("button", { name: "Delete session bravo" })).toBeInTheDocument();
  });

  it("surfaces a failed delete and keeps the session listed", async () => {
    await loadTwoSessions();
    boundary.state.deleteThrows = true;

    fireEvent.click(screen.getByRole("button", { name: "Delete session alpha" }));
    fireEvent.click(screen.getByRole("button", { name: "Confirm delete session alpha" }));

    await waitFor(() =>
      expect(screen.getByText("Could not delete that session.")).toBeInTheDocument(),
    );
    expect(screen.getByRole("button", { name: "Delete session alpha" })).toBeInTheDocument();
  });
});

describe("Session reflection editing from Insights", () => {
  it("keeps an unsaved reflection until it is saved or cancelled before deletion", async () => {
    boundary.state.history = [rawSummary("a", 50, 0, 0), rawSummary("b", 60, 0, 0)];
    renderApp("review");
    fireEvent.click(screen.getByText("Session management"));
    const editButtons = await screen.findAllByRole("button", { name: "Edit reflection" });
    fireEvent.click(editButtons[0]);
    fireEvent.change(screen.getByLabelText("What got done?"), {
      target: { value: "unsaved result" },
    });

    for (const button of screen.getAllByRole("button", { name: /^Delete session/ })) {
      expect(button).toBeDisabled();
    }
    expect(screen.getByLabelText("What got done?")).toHaveValue("unsaved result");
    expect(boundary.invoke).not.toHaveBeenCalledWith("delete_session", expect.anything());

    fireEvent.click(screen.getByRole("button", { name: "Cancel" }));
    for (const button of screen.getAllByRole("button", { name: /^Delete session/ })) {
      expect(button).toBeEnabled();
    }
  });

  it("prefills and updates an existing reflection", async () => {
    const summary = rawSummary("a", 50, 0, 0);
    boundary.state.history = [
      {
        ...summary,
        record: {
          ...summary.record,
          reflection_done: "old result",
          reflection_next_step: "old next",
        },
      },
    ];
    renderApp("review");
    fireEvent.click(screen.getByText("Session management"));
    await screen.findByRole("button", { name: "Edit reflection" });

    fireEvent.click(screen.getByRole("button", { name: "Edit reflection" }));
    fireEvent.change(screen.getByLabelText("What got done?"), { target: { value: "new result" } });
    fireEvent.click(screen.getByRole("button", { name: "Save reflection" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("save_session_reflection", {
        sessionId: "a",
        done: "new result",
        nextStep: "old next",
      }),
    );
    expect(await screen.findByText("Reflection updated.")).toBeInTheDocument();
  });

  it("keeps the editor open when a reflection update fails", async () => {
    boundary.state.reflectionThrows = true;
    const summary = rawSummary("a", 50, 0, 0);
    boundary.state.history = [
      {
        ...summary,
        record: {
          ...summary.record,
          reflection_done: "old result",
          reflection_next_step: "old next",
        },
      },
    ];
    renderApp("review");

    fireEvent.click(screen.getByText("Session management"));
    fireEvent.click(await screen.findByRole("button", { name: "Edit reflection" }));
    fireEvent.change(screen.getByLabelText("What got done?"), { target: { value: "new result" } });
    fireEvent.click(screen.getByRole("button", { name: "Save reflection" }));

    expect(await screen.findByText("Could not update that reflection.")).toBeInTheDocument();
    expect(screen.getByLabelText("What got done?")).toHaveValue("new result");
  });
});

describe("Focus summary card", () => {
  it("consolidates headline metrics and peak focus in the overview", async () => {
    boundary.state.summary = {
      sample_count: 40,
      session_count: 2,
      completed_session_count: 2,
      avg_focus_score: 72.4,
      longest_focus_secs: 18,
      distracted_fraction: 0.15,
    };
    boundary.state.focusSummary = {
      sample_count: 40,
      avg_focus_score: 72.4,
      peak_focus_score: 95,
      distracted_samples: 6,
      distracted_fraction: 0.15,
      longest_focus_secs: 18,
    };
    renderApp("review");

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("get_focus_summary", { window: "7d" }),
    );

    const card = focusSummaryCard();
    const tiles = within(card);
    expect(tiles.getByText("72")).toBeInTheDocument();
    expect(tiles.getByText("Peak focus 95")).toBeInTheDocument();
    expect(tiles.getByText(/15% of predictions were distracted/)).toBeInTheDocument();
    // Roadmap 10.13. A duration, not a bare row count: 18 seconds reads as "18s". The tile
    // used to show the number of consecutive non-distracted prediction rows under a
    // time-like label.
    expect(tiles.getByText("18s")).toBeInTheDocument();
  });

  it("shows an empty state when there are no predictions yet", async () => {
    boundary.state.focusSummary = { sample_count: 0 };
    renderApp("review");

    const card = focusSummaryCard();
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("get_focus_summary", { window: "7d" }),
    );
    expect(within(card).getByText(/No summary data for this range yet/i)).toBeInTheDocument();
  });
});

describe("Review interval provenance", () => {
  const summaryPill = () =>
    within(screen.getByRole("heading", { name: "Summary" }).closest("section") as HTMLElement)
      .getAllByText(/Last|All time|Since/)[0];

  it("keeps cards labelled with the interval they hold when a range change fails, and Retry works", async () => {
    boundary.state.history = [rawSummary("a", 50, 10, 0)];
    renderApp("review");
    await waitFor(() => expect(screen.getByRole("button", { name: "Last 7 days" })).toBeEnabled());
    expect(summaryPill()).toHaveTextContent("Last 7 days");

    boundary.state.failWindows.add("30d");
    fireEvent.click(screen.getByRole("button", { name: "Last 30 days" }));
    // The button is pressed, the load failed, and the cards still say what they show.
    expect(await screen.findByRole("alert")).toHaveTextContent(/Still showing Last 7 days/);
    expect(screen.getByRole("button", { name: "Last 30 days" })).toHaveAttribute(
      "aria-pressed",
      "true",
    );
    expect(summaryPill()).toHaveTextContent("Last 7 days");

    boundary.state.failWindows.clear();
    fireEvent.click(screen.getByRole("button", { name: "Retry" }));
    await waitFor(() => expect(summaryPill()).toHaveTextContent("Last 30 days"));
    expect(screen.queryByRole("alert")).toBeNull();
  });

  it("marks the live cards as outside the selected interval and surfaces the session cap", async () => {
    boundary.state.history = [rawSummary("a", 50, 10, 0)];
    boundary.state.summary = {
      window: "7d",
      sample_count: 3,
      session_count: 600,
      completed_session_count: 600,
      session_limit: 500,
      sessions_truncated: true,
    };
    renderApp("review");
    await waitFor(() => expect(screen.getByRole("button", { name: "Last 7 days" })).toBeEnabled());

    expect(screen.getByText(/live · latest/)).toBeInTheDocument();
    expect(screen.getByText(/live · this session/)).toBeInTheDocument();
    expect(screen.getByText(/Recent Predictions and Context Timeline are live/)).toBeInTheDocument();
    // The per-session chart reads the same capped list the report does, and says so.
    const chart = screen
      .getByRole("heading", { name: "Focus per session" })
      .closest("section") as HTMLElement;
    expect(within(chart).getByText(/latest 500 sessions only/)).toBeInTheDocument();
  });
});

describe("Review first-run states", () => {
  it("offers one shared time-range control for every Review card", async () => {
    renderApp("review");

    expect(await screen.findByRole("button", { name: "Last 24h" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Last 7 days" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Last 30 days" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "All time" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Custom" })).toBeInTheDocument();
  });

  it("explains empty charts and overview without presenting zeroes as insights", async () => {
    boundary.state.history = [];
    boundary.state.analytics = { sample_count: 0 };
    // The backend counts an active session before its first prediction. That is still a
    // first-run state, not one zero-valued session worth exporting.
    boundary.state.summary = {
      window: "day",
      sample_count: 0,
      session_count: 1,
      completed_session_count: 0,
    };
    boundary.state.focusSummary = { sample_count: 0 };
    renderApp("review");

    expect(await screen.findByText(/No completed sessions yet/i)).toBeInTheDocument();
    expect(screen.getByText(/No prediction data yet/i)).toBeInTheDocument();
    expect(screen.getByText(/No summary data for this range yet/i)).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Export summary" })).toBeDisabled();
  });

  it("keeps a completed zero-prediction session available for review and export", async () => {
    boundary.state.summary = {
      window: "day",
      sample_count: 0,
      session_count: 1,
      completed_session_count: 1,
      focus_seconds: 12,
    };
    renderApp("review");

    const summary = screen.getByRole("heading", { name: "Summary" }).closest("section");
    expect(summary).not.toBeNull();
    await waitFor(() =>
      expect(screen.getByRole("button", { name: "Export summary" })).toBeEnabled(),
    );
    expect(
      within(summary as HTMLElement).queryByText(/No summary data for this range yet/i),
    ).toBeNull();
  });
});
