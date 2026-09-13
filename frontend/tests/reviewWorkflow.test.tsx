import { StrictMode } from "react";
import { act, cleanup, renderHook, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const boundary = vi.hoisted(() => {
  const invoke = vi.fn(async (command: string): Promise<unknown> => {
    switch (command) {
      case "get_session_history":
        return [];
      case "get_analytics":
      case "get_summary_report":
      case "get_focus_summary":
        return {};
      default:
        return null;
    }
  });
  return { invoke };
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
