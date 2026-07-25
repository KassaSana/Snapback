import { cleanup, fireEvent, render, screen } from "@testing-library/react";
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
    thrashScore: 0.05,
    driftScore: 0.02,
    goalAlignment: 0.5,
    timestamp: "2026-07-25T04:55:24Z",
    modelId: "heuristic:snapback-features-v1-31",
    ...overrides,
  }) as PredictionRecord;

type Overrides = Partial<React.ComponentProps<typeof FocusStateHero>>;

function renderHero(overrides: Overrides = {}) {
  const props = {
    goal: null,
    hyperfocusNote: null,
    labelStatus: null,
    onConfirmVerdict: vi.fn(),
    onCorrectVerdict: vi.fn(),
    onDismissSnapback: vi.fn(),
    prediction: prediction(),
    riskClass: "low",
    sessionActive: true,
    snapbackNote: null,
    ...overrides,
  };
  render(<FocusStateHero {...props} />);
  return props;
}

describe("FocusStateHero", () => {
  it("leads with the focus state, not the score (ADR-0003)", () => {
    renderHero();
    expect(screen.getByText("Productive")).toBeInTheDocument();
  });

  it("renders the score and risk as whole numbers", () => {
    renderHero();

    // 71.24 -> "71", 0.2138 -> "21%". A decimal would claim precision the model has not
    // earned while the score's scale is an open decision (Roadmap 5.3/5.4/1.2/7.7).
    const secondary = screen.getByText(/focus/).textContent ?? "";
    expect(secondary).toContain("focus 71");
    expect(secondary).toContain("risk 21%");
    expect(secondary).not.toContain("71.2");
    expect(secondary).not.toContain("21.4%");
  });

  it("says it is waiting rather than showing a fake zero before the first prediction", () => {
    renderHero({ prediction: null });

    expect(screen.getByText("Waiting for signal")).toBeInTheDocument();
    expect(screen.getByText(/capture is warming up/)).toBeInTheDocument();
    expect(screen.queryByText(/focus 0/)).not.toBeInTheDocument();
    // Nothing to rate yet, so the control stays out of the way.
    expect(screen.queryByRole("button", { name: /is right/ })).not.toBeInTheDocument();
  });

  it("explains the verdict with the signals the classifier actually measured", () => {
    renderHero({
      goal: "fix the overlay",
      prediction: prediction({ thrashScore: 0.05, driftScore: 0.02, goalAlignment: 0.9 }),
    });

    expect(screen.getByText("no app switching")).toBeInTheDocument();
    expect(screen.getByText("settled in one window")).toBeInTheDocument();
    expect(screen.getByText(/window matches/)).toBeInTheDocument();
  });

  it("names switching and churn when those are what drove the verdict", () => {
    renderHero({
      prediction: prediction({ focusState: "DISTRACTED", thrashScore: 0.8, driftScore: 0.7 }),
      riskClass: "high",
    });

    expect(screen.getByText("switching apps often")).toBeInTheDocument();
    expect(screen.getByText("tab and title churn")).toBeInTheDocument();
  });

  // The honesty case: deep_work_score is built from absence of switching, so a quiet
  // screen scores the same whether you are reading a spec or watching a film.
  it("admits it cannot tell a quiet screen apart when nothing else supports the verdict", () => {
    renderHero({ prediction: prediction({ focusState: "DEEP_FOCUS", goalAlignment: 0.5 }) });

    expect(screen.getByText(/quiet screen looks the same/)).toBeInTheDocument();
  });

  it("drops the caveat once the goal corroborates the verdict", () => {
    renderHero({
      goal: "fix the overlay",
      prediction: prediction({ focusState: "DEEP_FOCUS", goalAlignment: 0.9 }),
    });

    expect(screen.queryByText(/quiet screen looks the same/)).not.toBeInTheDocument();
  });

  it("does not caveat a negative verdict", () => {
    renderHero({
      prediction: prediction({ focusState: "DISTRACTED", thrashScore: 0.1, driftScore: 0.1 }),
    });

    expect(screen.queryByText(/quiet screen looks the same/)).not.toBeInTheDocument();
  });

  it("records agreement with one click", () => {
    const props = renderHero();

    fireEvent.click(screen.getByRole("button", { name: "This reading is right" }));
    expect(props.onConfirmVerdict).toHaveBeenCalledTimes(1);
  });

  it("asks what the state really was before recording a disagreement", () => {
    const props = renderHero();

    fireEvent.click(screen.getByRole("button", { name: "This reading is wrong" }));
    expect(screen.getByText("What was it really?")).toBeInTheDocument();

    // The predicted state is not offered — you cannot correct something to itself.
    expect(screen.queryByRole("button", { name: "Productive" })).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Distracted" }));
    expect(props.onCorrectVerdict).toHaveBeenCalledWith("DISTRACTED");
  });

  it("tells the user why rating is unavailable instead of showing a dead control", () => {
    renderHero({ sessionActive: false });

    expect(screen.getByText(/Start a session to rate/)).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "This reading is right" })).not.toBeInTheDocument();
  });

  it("keeps the snapback callout prominent and dismissable", () => {
    const props = renderHero({
      prediction: prediction({ focusState: "DISTRACTED" }),
      riskClass: "high",
      snapbackNote: "You drifted from: fix the overlay",
    });

    expect(screen.getByText(/You drifted from/)).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Dismiss" }));
    expect(props.onDismissSnapback).toHaveBeenCalledTimes(1);
  });
});
