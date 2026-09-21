import { StrictMode } from "react";
import { act, cleanup, renderHook, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const boundary = vi.hoisted(() => {
  const state: {
    /** `get_analytics` for this window waits on the held promise; the test releases it. */
    hold: Record<string, Promise<unknown>>;
    /** Windows whose loads reject. */
    fail: Set<string>;
  } = { hold: {}, fail: new Set() };

  const invoke = vi.fn(async (command: string, args?: Record<string, unknown>): Promise<unknown> => {
    const window = String(args?.window ?? "");
    switch (command) {
      case "get_session_history":
        return [];
      case "get_analytics": {
        if (state.fail.has(window)) throw new Error(`${window} unavailable`);
        const held = state.hold[window];
        if (held) return held;
        // The window travels with the answer so a test can tell whose data landed.
        return { sample_count: window === "all" ? 999 : window === "30d" ? 30 : 7 };
      }
      case "get_summary_report":
      case "get_focus_summary":
        return {};
      default:
        return null;
    }
  });
  return { invoke, state };
});

vi.mock("../src/bridge", () => ({
  invoke: boundary.invoke,
  listen: vi.fn(async () => () => {}),
}));

import { useReviewWorkflow } from "../src/useReviewWorkflow";

const reviewCallCount = () =>
  boundary.invoke.mock.calls.filter(([command]) =>
    ["get_analytics", "get_summary_report", "get_focus_summary", "get_session_history"].includes(
      command,
    ),
  ).length;

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.hold = {};
  boundary.state.fail = new Set();
});

afterEach(() => cleanup());

describe("useReviewWorkflow hydration", () => {
  it("does not load while hidden, coalesces StrictMode effects, and caches re-entry", async () => {
    const { rerender } = renderHook(
      ({ active }: { active: boolean }) => useReviewWorkflow({ active }),
      {
        initialProps: { active: false },
        wrapper: StrictMode,
      },
    );

    expect(reviewCallCount()).toBe(0);
    rerender({ active: true });
    await waitFor(() => expect(reviewCallCount()).toBe(4));

    rerender({ active: false });
    rerender({ active: true });
    expect(reviewCallCount()).toBe(4);
  });

  it("defers invalidated data until Review becomes active", async () => {
    const { result, rerender } = renderHook(
      ({ active }: { active: boolean }) => useReviewWorkflow({ active }),
      { initialProps: { active: true } },
    );

    await waitFor(() => expect(reviewCallCount()).toBe(4));
    rerender({ active: false });
    act(() => result.current.invalidateReview());
    expect(reviewCallCount()).toBe(4);

    rerender({ active: true });
    await waitFor(() => expect(reviewCallCount()).toBe(8));
  });
});

// Slice 5 of the Astra review (Roadmap 10.11): the interval on a card is the interval its
// data came from. Selecting a new range must not relabel the old numbers, whether the new
// load is still running, failed, or arrived out of order.
describe("useReviewWorkflow interval provenance", () => {
  it("keeps the loaded interval on the data while a new range loads, and after it fails", async () => {
    const { result } = renderHook(() => useReviewWorkflow({ active: true }));
    await waitFor(() => expect(result.current.analytics.sampleCount).toBe(7));
    expect(result.current.displayedRange).toEqual({ preset: "7d" });
    expect(result.current.staleInterval).toBe(false);

    // The 30-day load stalls. The selection moves; what is described does not.
    let release: (value: unknown) => void = () => {};
    boundary.state.hold["30d"] = new Promise((resolve) => {
      release = resolve;
    });
    act(() => result.current.setRange({ preset: "30d" }));
    expect(result.current.range).toEqual({ preset: "30d" });
    expect(result.current.displayedRange).toEqual({ preset: "7d" });
    expect(result.current.staleInterval).toBe(true);
    expect(result.current.analytics.sampleCount).toBe(7);

    // It lands: now the cards describe 30 days.
    await act(async () => {
      release({ sample_count: 30 });
    });
    await waitFor(() => expect(result.current.analytics.sampleCount).toBe(30));
    expect(result.current.displayedRange).toEqual({ preset: "30d" });
    expect(result.current.staleInterval).toBe(false);

    // A failed load leaves the previous data, still labelled as the previous interval.
    boundary.state.fail.add("all");
    act(() => result.current.setRange({ preset: "all" }));
    await waitFor(() => expect(result.current.error).not.toBeNull());
    expect(result.current.analytics.sampleCount).toBe(30);
    expect(result.current.displayedRange).toEqual({ preset: "30d" });
    expect(result.current.staleInterval).toBe(true);

    // Retry is a real retry: the same selection, asked again.
    boundary.state.fail.clear();
    await act(async () => {
      await result.current.refreshReview();
    });
    await waitFor(() => expect(result.current.analytics.sampleCount).toBe(999));
    expect(result.current.error).toBeNull();
    expect(result.current.displayedRange).toEqual({ preset: "all" });
    expect(result.current.staleInterval).toBe(false);
  });

  it("never lets an older response overwrite a newer one", async () => {
    const { result } = renderHook(() => useReviewWorkflow({ active: true }));
    await waitFor(() => expect(result.current.analytics.sampleCount).toBe(7));

    let releaseThirty: (value: unknown) => void = () => {};
    boundary.state.hold["30d"] = new Promise((resolve) => {
      releaseThirty = resolve;
    });
    act(() => result.current.setRange({ preset: "30d" }));
    // The user moves on before 30d answers; "all" answers immediately.
    act(() => result.current.setRange({ preset: "all" }));
    await waitFor(() => expect(result.current.analytics.sampleCount).toBe(999));
    expect(result.current.displayedRange).toEqual({ preset: "all" });

    // The late 30d answer must not land on top of it -- neither the data nor the label.
    await act(async () => {
      releaseThirty({ sample_count: 30 });
      await Promise.resolve();
    });
    expect(result.current.analytics.sampleCount).toBe(999);
    expect(result.current.displayedRange).toEqual({ preset: "all" });
    expect(result.current.staleInterval).toBe(false);
  });
});
