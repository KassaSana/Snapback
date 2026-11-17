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
          reflectionDone: args?.done ?? null,
          reflectionNextStep: args?.nextStep ?? null,
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

// Deleting and editing a session happen in the session explorer's detail panel.
describe("Session deletion from the explorer", () => {
  const goalSummary = (id: string, goal: string) => {
    const base = rawSummary(id, 50, 0, 0);
    return { record: { ...base.record, goal }, recap: { ...base.recap, goal } };
  };

  const row = (goal: string) => screen.queryByRole("button", { name: new RegExp(`^${goal}`) });

  const loadTwoSessions = async () => {
    boundary.state.history = [goalSummary("a", "alpha"), goalSummary("b", "bravo")];
    renderApp("review");
    await waitFor(() => expect(row("alpha")).not.toBeNull());
    fireEvent.click(row("alpha")!);
  };

  const deleteAlpha = () => {
    fireEvent.click(screen.getByRole("button", { name: "Delete session alpha" }));
    fireEvent.click(screen.getByRole("button", { name: "Confirm delete session alpha" }));
  };

  it("names the selected session on its delete button", async () => {
    await loadTwoSessions();
    expect(screen.getByRole("button", { name: "Delete session alpha" })).toBeInTheDocument();
    fireEvent.click(row("bravo")!);
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
    expect(row("alpha")).toBeNull();
    expect(row("bravo")).not.toBeNull();
  });

  it("cancelling leaves the session and never reaches the backend", async () => {
    await loadTwoSessions();

    fireEvent.click(screen.getByRole("button", { name: "Delete session alpha" }));
    fireEvent.click(screen.getByRole("button", { name: "Cancel" }));

    expect(boundary.invoke).not.toHaveBeenCalledWith("delete_session", expect.anything());
    expect(screen.getByRole("button", { name: "Delete session alpha" })).toBeInTheDocument();
  });

  // The native command returns false when no row matched, so the wording must not claim a delete.
  it("reports a stale row honestly instead of claiming a delete", async () => {
    await loadTwoSessions();
    // Something else removed it between the list load and the click.
    boundary.state.history = [goalSummary("b", "bravo")];
    deleteAlpha();

    await waitFor(() =>
      expect(screen.getByText("That session was already gone.")).toBeInTheDocument(),
    );
    expect(screen.queryByText("Session deleted.")).toBeNull();
    expect(row("alpha")).toBeNull();
  });

  // The delete is authoritative once it returns, so a failed refetch must not bring the row back.
  it("keeps the row gone even when the follow-up refresh fails", async () => {
    await loadTwoSessions();
    boundary.state.historyThrows = true;
    deleteAlpha();

    await waitFor(() => expect(row("alpha")).toBeNull());
    expect(row("bravo")).not.toBeNull();
  });

  it("surfaces a failed delete and keeps the session listed", async () => {
    await loadTwoSessions();
    boundary.state.deleteThrows = true;
    deleteAlpha();

    await waitFor(() =>
      expect(screen.getByText("Could not delete that session.")).toBeInTheDocument(),
    );
    expect(row("alpha")).not.toBeNull();
  });
});

describe("Session reflection editing from the explorer", () => {
  const withReflection = () => {
    const summary = rawSummary("a", 50, 0, 0);
    return {
      ...summary,
      record: { ...summary.record, reflectionDone: "old result", reflectionNextStep: "old next" },
    };
  };

  it("keeps an unsaved reflection until it is saved or cancelled before deletion", async () => {
    boundary.state.history = [rawSummary("a", 50, 0, 0), rawSummary("b", 60, 0, 0)];
    renderApp("review");
    fireEvent.click(await screen.findByRole("button", { name: "Add a reflection" }));
    fireEvent.change(screen.getByLabelText("Done"), { target: { value: "unsaved result" } });

    expect(screen.getByRole("button", { name: /^Delete session/ })).toBeDisabled();
    expect(screen.getByLabelText("Done")).toHaveValue("unsaved result");
    expect(boundary.invoke).not.toHaveBeenCalledWith("delete_session", expect.anything());

    fireEvent.click(screen.getByRole("button", { name: "Cancel" }));
    expect(screen.getByRole("button", { name: /^Delete session/ })).toBeEnabled();
  });

  it("prefills and updates an existing reflection", async () => {
    boundary.state.history = [withReflection()];
    renderApp("review");

    fireEvent.click(await screen.findByRole("button", { name: "Edit reflection" }));
    expect(screen.getByLabelText("Done")).toHaveValue("old result");
    fireEvent.change(screen.getByLabelText("Done"), { target: { value: "new result" } });
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
    boundary.state.history = [withReflection()];
    renderApp("review");

    fireEvent.click(await screen.findByRole("button", { name: "Edit reflection" }));
    fireEvent.change(screen.getByLabelText("Done"), { target: { value: "new result" } });
    fireEvent.click(screen.getByRole("button", { name: "Save reflection" }));

    expect(await screen.findByText("Could not update that reflection.")).toBeInTheDocument();
    expect(screen.getByLabelText("Done")).toHaveValue("new result");
  });
});

describe("Focus summary card", () => {
  it("consolidates headline metrics and peak focus in the overview", async () => {
    boundary.state.summary = {
      sampleCount: 40,
      sessionCount: 2,
      completedSessionCount: 2,
      avgFocusScore: 72.4,
      longestFocusSecs: 18,
      distractedFraction: 0.15,
    };
    boundary.state.focusSummary = {
      sampleCount: 40,
      avgFocusScore: 72.4,
      peakFocusScore: 95,
      distractedSamples: 6,
      distractedFraction: 0.15,
      longestFocusSecs: 18,
    };
    renderApp("review");

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("get_focus_summary", { window: "7d" }),
    );

    const card = focusSummaryCard();
    const tiles = within(card);
    expect(tiles.getByText("72")).toBeInTheDocument();
    expect(tiles.getByText("Peak focus 95.")).toBeInTheDocument();
    expect(tiles.getByText(/15% of the time looked distracted/)).toBeInTheDocument();
    // A duration, not a bare row count: 18 seconds reads as "18s". The tile
    // used to show the number of consecutive non-distracted prediction rows under a
    // time-like label.
    expect(tiles.getByText("18s")).toBeInTheDocument();
  });

  it("shows an empty state when there are no predictions yet", async () => {
    boundary.state.focusSummary = { sampleCount: 0 };
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
    const rangeBar = screen.getByRole("heading", { name: "Time range" }).closest("section") as HTMLElement;
    expect(await within(rangeBar).findByRole("alert")).toHaveTextContent(/Still showing Last 7 days/);
    expect(screen.getByRole("button", { name: "Last 30 days" })).toHaveAttribute(
      "aria-pressed",
      "true",
    );
    expect(summaryPill()).toHaveTextContent("Last 7 days");

    boundary.state.failWindows.clear();
    fireEvent.click(screen.getByRole("button", { name: "Retry" }));
    await waitFor(() => expect(summaryPill()).toHaveTextContent("Last 30 days"));
    expect(within(rangeBar).queryByRole("alert")).toBeNull();
  });

  it("keeps Review free of live feeds and surfaces the session cap", async () => {
    boundary.state.history = [rawSummary("a", 50, 10, 0)];
    boundary.state.summary = {
      window: "7d",
      sampleCount: 3,
      sessionCount: 600,
      completedSessionCount: 600,
      sessionLimit: 500,
      sessionsTruncated: true,
    };
    renderApp("review");
    await waitFor(() => expect(screen.getByRole("button", { name: "Last 7 days" })).toBeEnabled());

    expect(screen.queryByRole("heading", { name: "Recent readings" })).not.toBeInTheDocument();
    expect(screen.getByText(/selected session’s insight and context describe that session/)).toBeInTheDocument();
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
    boundary.state.analytics = { sampleCount: 0 };
    // The backend counts an active session before its first prediction. That is still a
    // first-run state, not one zero-valued session worth exporting.
    boundary.state.summary = {
      window: "day",
      sampleCount: 0,
      sessionCount: 1,
      completedSessionCount: 0,
    };
    boundary.state.focusSummary = { sampleCount: 0 };
    renderApp("review");

    expect(await screen.findByText(/No completed sessions yet/i)).toBeInTheDocument();
    expect(screen.getByText(/No prediction data yet/i)).toBeInTheDocument();
    expect(screen.getByText(/No summary data for this range yet/i)).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Export summary" })).toBeDisabled();
  });

  it("keeps a completed zero-prediction session available for review and export", async () => {
    boundary.state.summary = {
      window: "day",
      sampleCount: 0,
      sessionCount: 1,
      completedSessionCount: 1,
      focusSeconds: 12,
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
