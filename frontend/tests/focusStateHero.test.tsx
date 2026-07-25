import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { FocusStateHero } from "../src/FocusStateHero";
import type { PredictionRecord } from "../src/api";

afterEach(() => {
  cleanup();
});

const prediction = (overrides: Partial<PredictionRecord> = {}): PredictionRecord =>
  ({
    sessionId: "s-1",
    focusState: "PRODUCTIVE",
    focusScore: 71.24,
    distractionRisk: 0.2138,
    timestamp: "2026-07-25T04:55:24Z",
    ...overrides,
  }) as PredictionRecord;

describe("FocusStateHero", () => {
  it("leads with the focus state, not the score (ADR-0003)", () => {
    render(
      <FocusStateHero
        hyperfocusNote={null}
        onDismissSnapback={vi.fn()}
        prediction={prediction()}
        riskClass="low"
        snapbackNote={null}
      />,
    );

    // The state is the hero: it is what survives a rescaling of focus_score.
    expect(screen.getByText("Productive")).toBeInTheDocument();
  });

  it("renders the score and risk as whole numbers", () => {
    render(
      <FocusStateHero
        hyperfocusNote={null}
        onDismissSnapback={vi.fn()}
        prediction={prediction()}
        riskClass="low"
        snapbackNote={null}
      />,
    );

    // 71.24 -> "71", 0.2138 -> "21%". A decimal would claim precision the model has not
    // earned while the score's scale is still an open decision (Roadmap 5.3/5.4/1.2/7.7).
    const secondary = screen.getByText(/focus/).textContent ?? "";
    expect(secondary).toContain("focus 71");
    expect(secondary).toContain("risk 21%");
    expect(secondary).not.toContain("71.2");
    expect(secondary).not.toContain("21.4%");
  });

  it("says it is waiting rather than showing a fake zero before the first prediction", () => {
    render(
      <FocusStateHero
        hyperfocusNote={null}
        onDismissSnapback={vi.fn()}
        prediction={null}
        riskClass="unknown"
        snapbackNote={null}
      />,
    );

    expect(screen.getByText("Waiting for signal")).toBeInTheDocument();
    expect(screen.getByText(/capture is warming up/)).toBeInTheDocument();
    // "0" would read as a measurement; there is no measurement yet.
    expect(screen.queryByText(/focus 0/)).not.toBeInTheDocument();
  });

  it("keeps the snapback callout prominent and dismissable", () => {
    const onDismiss = vi.fn();
    render(
      <FocusStateHero
        hyperfocusNote={null}
        onDismissSnapback={onDismiss}
        prediction={prediction({ focusState: "DISTRACTED" })}
        riskClass="high"
        snapbackNote="You drifted from: fix the overlay"
        />,
    );

    expect(screen.getByText(/You drifted from/)).toBeInTheDocument();
    screen.getByRole("button", { name: "Dismiss" }).click();
    expect(onDismiss).toHaveBeenCalledTimes(1);
  });
});
