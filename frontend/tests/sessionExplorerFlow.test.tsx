import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Roadmap 2.9. The session explorer against the mocked native boundary: the list, the detail
// it loads per session, and the two actions that must not do more than they say.
const boundary = vi.hoisted(() => {
  const state = {
    history: [] as Record<string, unknown>[],
    failDetail: false,
  };
  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>): Promise<unknown> => {
    switch (cmd) {
      case "get_session_focus_curve":
        if (state.failDetail) throw new Error("storage busy");
        return [
          { startMs: Date.parse("2026-09-21T09:00:00Z"), sampleCount: 120, avgFocusScore: 64 },
          { startMs: Date.parse("2026-09-21T09:30:00Z"), sampleCount: 90, avgFocusScore: 82.4 },
        ];
      case "get_context_timeline":
        return args?.sessionId
          ? [
              { app_name: "Code.exe", timestamp_ms: 1 },
              { app_name: "Code.exe", timestamp_ms: 2 },
              { app_name: "chrome.exe", timestamp_ms: 3 },
            ]
          : [];
      case "get_health":
        return {
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
        };
      case "get_settings":
        return { default_focus_mode: "normal" };
      case "get_session_history":
        return state.history;
      case "get_focus_summary":
      case "get_analytics":
      case "get_summary_report":
        return {};
      case "get_prediction_history":
      case "get_app_rules":
        return [];
      default:
        return null;
    }
  });
  const listen = vi.fn(async () => () => {});
  return { state, invoke, listen };
});

vi.mock("../src/bridge", () => ({ invoke: boundary.invoke, listen: boundary.listen }));

import App from "../src/App";
import type { SessionSummary } from "../src/api";
import { SessionExplorerCard } from "../src/SessionExplorerCard";

const summary = (
  id: string,
  goal: string,
  startedAt: string,
  extra: Partial<SessionSummary["record"]> = {},
): SessionSummary => ({
  record: {
    sessionId: id,
    goal,
    status: "COMPLETED",
    focusMode: "deep",
    startedAtMs: Date.parse(startedAt),
    endedAtMs: Date.parse(startedAt) + 3_600_000,
    reflectionDone: null,
    reflectionNextStep: null,
    ...extra,
  },
  recap: {
    sessionId: id,
    goal,
    durationSecs: 3600,
    activeSecs: 3000,
    avgFocusScore: 71,
    avgDistractionRisk: 0.3,
    snapbackCount: 2,
    thrashSpikes: 0,
    deepFocusPct: 40,
  },
});

const sessions = [
  summary("a", "Write the explorer", "2026-09-21T09:00:00Z", {
    reflectionDone: "List and detail",
    reflectionNextStep: "Tests",
  }),
  summary("b", "Review the PR", "2026-09-22T14:00:00Z"),
];

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.history = [];
  boundary.state.failDetail = false;
});

afterEach(() => {
  cleanup();
});

describe("Session explorer", () => {
  it("lists sessions newest first, one group per day", () => {
    render(
      <SessionExplorerCard
        sessionHistory={sessions}
        rangeLabel="Last 7 days"
        sessionActive={false}
        onStartAgain={() => {}}
      />,
    );
    const rows = screen.getAllByRole("button", { pressed: false });
    expect(rows[0]).toHaveTextContent("Review the PR");
    expect(rows[1]).toHaveTextContent("Write the explorer");
    expect(rows[1]).toHaveTextContent("Deep · 50m · focus 71");
    expect(screen.getAllByRole("heading", { level: 3 })).toHaveLength(2);
  });

  it("opens a session's detail: stats, reflection, its own curve and where it was spent", async () => {
    render(
      <SessionExplorerCard
        sessionHistory={sessions}
        rangeLabel="Last 7 days"
        sessionActive={false}
        onStartAgain={() => {}}
      />,
    );
    fireEvent.click(screen.getByRole("button", { name: /Write the explorer/ }));
    const detail = screen.getByRole("region", { name: "Session: Write the explorer" });

    expect(within(detail).getByText("Snapbacks").nextElementSibling).toHaveTextContent("2");
    expect(within(detail).getByText(/List and detail/)).toBeInTheDocument();
    await within(detail).findByRole("img", { name: /Focus over the session/ });

    // The curve and context were asked for this session, not the live one.
    expect(boundary.invoke).toHaveBeenCalledWith("get_session_focus_curve", {
      sessionId: "a",
      buckets: 60,
    });
    expect(boundary.invoke).toHaveBeenCalledWith("get_context_timeline", {
      sessionId: "a",
      limit: 500,
    });

    fireEvent.click(within(detail).getByText("Show data"));
    expect(within(detail).getByRole("table")).toHaveTextContent("focus 82 of 100 · 90 samples");

    const apps = within(detail).getAllByRole("listitem");
    expect(apps[0]).toHaveTextContent("Code.exe2 samples");
    expect(apps[1]).toHaveTextContent("chrome.exe1 sample");
  });

  it("says so when the detail cannot load", async () => {
    boundary.state.failDetail = true;
    render(
      <SessionExplorerCard
        sessionHistory={sessions}
        rangeLabel="Last 7 days"
        sessionActive={false}
        onStartAgain={() => {}}
      />,
    );
    fireEvent.click(screen.getByRole("button", { name: /Review the PR/ }));
    expect(await screen.findByRole("alert")).toHaveTextContent("Could not load");
  });

  it("deletes only after a confirmation", async () => {
    const onDelete = vi.fn();
    render(
      <SessionExplorerCard
        sessionHistory={sessions}
        rangeLabel="Last 7 days"
        sessionActive={false}
        onStartAgain={() => {}}
        onDeleteSession={onDelete}
      />,
    );
    fireEvent.click(screen.getByRole("button", { name: /Review the PR/ }));
    fireEvent.click(screen.getByRole("button", { name: "Delete this session…" }));
    expect(onDelete).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole("button", { name: "Confirm delete" }));
    expect(onDelete).toHaveBeenCalledWith("b");
  });

  it("cannot start again while another session is running", () => {
    render(
      <SessionExplorerCard
        sessionHistory={sessions}
        rangeLabel="Last 7 days"
        sessionActive
        onStartAgain={() => {}}
      />,
    );
    fireEvent.click(screen.getByRole("button", { name: /Review the PR/ }));
    expect(screen.getByRole("button", { name: "Start this again" })).toBeDisabled();
    expect(screen.getByText(/A session is running/)).toBeInTheDocument();
  });

  it("Start this again fills the start form on Now and records nothing", async () => {
    const started = Date.now() - 3_600_000;
    boundary.state.history = [
      {
        record: {
          sessionId: "a",
          goal: "Write the explorer",
          status: "COMPLETED",
          focusMode: "deep",
          startedAtMs: started,
          endedAtMs: started + 3_000_000,
        },
        recap: {
          sessionId: "a",
          goal: "Write the explorer",
          durationSecs: 3000,
          avgFocusScore: 70,
          avgDistractionRisk: 0.3,
          snapbackCount: 0,
          thrashSpikes: 0,
          deepFocusPct: 30,
        },
      },
    ];
    render(<App />);
    fireEvent.click(await screen.findByRole("tab", { name: "Review" }));
    const explorer = (await screen.findByRole("heading", { name: "Sessions" })).closest(
      "section",
    ) as HTMLElement;
    fireEvent.click(await within(explorer).findByRole("button", { name: /Write the explorer/ }));
    fireEvent.click(within(explorer).getByRole("button", { name: "Start this again" }));

    await waitFor(() =>
      expect(screen.getByRole("tab", { name: "Now" })).toHaveAttribute("aria-selected", "true"),
    );
    expect(screen.getByPlaceholderText("Ship the snapback overlay")).toHaveValue(
      "Write the explorer",
    );
    expect((screen.getByLabelText("Focus mode") as HTMLSelectElement).value).toBe("deep");
    expect(boundary.invoke).not.toHaveBeenCalledWith("start_session", expect.anything());
  });
});
