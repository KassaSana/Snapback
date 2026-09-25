import { cleanup, render, screen, within } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import { PredictionHistoryCard } from "../src/ActivityCards";
import type { PredictionRecord } from "../src/api";

// Roadmap 10.3. Recent Predictions showed two bare numbers per row, and the risk level lived
// only in the chip's colour. These pin what the row says without sight of the colour.

afterEach(() => {
  cleanup();
});

const prediction = (distractionRisk: number): PredictionRecord => ({
  sessionId: "sess-1",
  focusScore: 61,
  distractionRisk,
  focusState: "PRODUCTIVE",
  thrashScore: 0.1,
  driftScore: 0.1,
  goalAlignment: 0.7,
  timestampMs: Date.parse("2026-09-22T10:00:00Z"),
  modelId: "heuristic:test",
  stateSource: "model",
});

describe("Recent Predictions without colour", () => {
  it("names each number and states the risk level in words", () => {
    render(
      <PredictionHistoryCard predictionHistory={[prediction(0.41)]} />,
    );
    const row = screen.getByRole("listitem");

    expect(within(row).getByText(/Focus score/).parentElement).toHaveTextContent("Focus score 61");
    const risk = within(row).getByText(/Distraction risk/).parentElement as HTMLElement;
    expect(risk).toHaveTextContent("Distraction risk 41.0%, medium risk");
    // The same level for pointer users, on hover.
    expect(risk).toHaveAttribute("title", "Medium risk");
  });
});
