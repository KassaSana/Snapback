import { act, cleanup, renderHook, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// A read that resolves after the state it was fetched for is gone must not land. The
// boundary here hands back deferred promises so the test controls exactly when each
// timeline / latest read completes, which is the only way to reproduce the race on purpose.
const boundary = vi.hoisted(() => {
  type Deferred = { resolve: (value: unknown) => void; reject: (error: unknown) => void };
  const pending: Array<{ command: string; args: unknown; deferred: Deferred }> = [];
  const invoke = vi.fn((command: string, args?: unknown): Promise<unknown> => {
    return new Promise((resolve, reject) => {
      pending.push({ command, args, deferred: { resolve, reject } });
    });
  });
  const take = (command: string) => {
    const index = pending.findIndex((p) => p.command === command);
    if (index < 0) throw new Error(`no pending ${command}`);
    return pending.splice(index, 1)[0];
  };
  return { invoke, pending, take };
});

vi.mock("../src/bridge", () => ({
  invoke: boundary.invoke,
  listen: vi.fn(async () => () => {}),
}));

import { useLiveData } from "../src/useLiveData";

const row = (appName: string) => ({
  app_name: appName,
  window_title: `${appName} window`,
  summary: appName,
  timestamp_ms: 1,
});

const prediction = (focusScore: number) => ({
  session_id: "s-1",
  focus_state: "PRODUCTIVE",
  focus_score: focusScore,
  distraction_risk: 0.1,
  thrash_score: 0,
  drift_score: 0,
  goal_alignment: 0.5,
  timestamp_ms: focusScore,
  model_id: "heuristic",
  state_source: "model",
});

beforeEach(() => {
  boundary.invoke.mockClear();
  boundary.pending.length = 0;
});

afterEach(() => cleanup());

describe("useLiveData stale reads", () => {
  it("drops a timeline read that resolves after a session switch", async () => {
    const { result } = renderHook(() => useLiveData());

    act(() => {
      void result.current.refreshContextTimeline("old-session");
    });
    const oldRead = boundary.take("get_context_timeline");
    act(() => {
      void result.current.refreshContextTimeline("new-session");
    });
    const newRead = boundary.take("get_context_timeline");

    // The new session's rows arrive first, then the old session's slow read completes.
    await act(async () => {
      newRead.deferred.resolve([row("Cursor")]);
    });
    await waitFor(() => expect(result.current.contextTimeline[0]?.appName).toBe("Cursor"));

    await act(async () => {
      oldRead.deferred.resolve([row("Chrome")]);
    });
    expect(result.current.contextTimeline.map((r) => r.appName)).toEqual(["Cursor"]);
  });

  it("drops a timeline read that resolves after the activity was cleared", async () => {
    const { result } = renderHook(() => useLiveData());

    act(() => {
      void result.current.refreshContextTimeline("s-1");
    });
    const read = boundary.take("get_context_timeline");

    act(() => {
      result.current.clearActivityData();
    });
    await act(async () => {
      read.deferred.resolve([row("Cursor")]);
    });
    // The rows the user just deleted must not come back.
    expect(result.current.contextTimeline).toEqual([]);
  });

  it("keeps the current timeline when a read fails", async () => {
    const { result } = renderHook(() => useLiveData());

    act(() => {
      void result.current.refreshContextTimeline("s-1");
    });
    await act(async () => {
      boundary.take("get_context_timeline").deferred.resolve([row("Cursor")]);
    });
    await waitFor(() => expect(result.current.contextTimeline).toHaveLength(1));

    act(() => {
      void result.current.refreshContextTimeline("s-1");
    });
    await act(async () => {
      boundary.take("get_context_timeline").deferred.reject(new Error("ipc down"));
    });
    // A failed read is not an empty timeline.
    expect(result.current.contextTimeline).toHaveLength(1);
  });

  it("drops a latest-prediction read that resolves after the activity was cleared", async () => {
    const { result } = renderHook(() => useLiveData());

    act(() => {
      void result.current.refreshLatest();
    });
    const read = boundary.take("get_latest_prediction");

    act(() => {
      result.current.clearActivityData();
    });
    await act(async () => {
      read.deferred.resolve(prediction(80));
    });
    expect(result.current.prediction).toBeNull();
    // The follow-up history read is never issued for a retired request.
    expect(boundary.pending.some((p) => p.command === "get_prediction_history")).toBe(false);
  });

  it("keeps the current prediction when the latest read fails", async () => {
    const { result } = renderHook(() => useLiveData());

    act(() => {
      void result.current.refreshLatest();
    });
    await act(async () => {
      boundary.take("get_latest_prediction").deferred.resolve(prediction(80));
    });
    await waitFor(() => expect(result.current.prediction?.focusScore).toBe(80));
    await act(async () => {
      boundary.take("get_prediction_history").deferred.resolve([]);
    });

    act(() => {
      void result.current.refreshLatest();
    });
    await act(async () => {
      boundary.take("get_latest_prediction").deferred.reject(new Error("ipc down"));
    });
    expect(result.current.prediction?.focusScore).toBe(80);
  });
});
