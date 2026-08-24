import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import { SummaryCard } from "../src/SummaryCard";
import type { SummaryReport } from "../src/api";

// Roadmap 2.19 Review half. The Summary card is where planned-versus-actual lands so it
// follows 10.11's shared range instead of inventing a second date control.

const baseReport = (overrides: Partial<SummaryReport> = {}): SummaryReport => ({
  window: "day",
  generatedAtMs: Date.parse("2026-08-12T12:00:00Z"),
  sessionCount: 1,
  completedSessionCount: 1,
  focusSeconds: 3600,
  sampleCount: 10,
  avgFocusScore: 70,
  distractedFraction: 0.1,
  longestFocusSecs: 600,
  topContextApp: "Cursor",
  attendedSeconds: 45 * 60,
  plannedMins: 120,
  ...overrides,
});

afterEach(() => cleanup());

describe("SummaryCard attended comparison", () => {
  it("shows planned-versus-actual when a target applies", () => {
    render(
      <SummaryCard
        exportStatus={null}
        onExport={() => {}}
        rangeLabel="Today"
        report={baseReport()}
      />,
    );
    expect(screen.getByText("Attended")).toBeTruthy();
    expect(screen.getByText("45m")).toBeTruthy();
    expect(screen.getByText(/of 2h 0m planned \(38%\)/)).toBeTruthy();
  });

  it("omits a fabricated plan when plannedMins is zero", () => {
    render(
      <SummaryCard
        exportStatus={null}
        onExport={() => {}}
        rangeLabel="30 days"
        report={baseReport({ window: "30d", plannedMins: 0, attendedSeconds: 90 * 60 })}
      />,
    );
    expect(screen.getByText("Attended")).toBeTruthy();
    expect(screen.getByText("1h 30m")).toBeTruthy();
    expect(screen.getByText("measured, not scored")).toBeTruthy();
    expect(screen.queryByText(/planned/)).toBeNull();
  });
});
