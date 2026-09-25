import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import { SummaryCard } from "../src/SummaryCard";
import { SessionReviewCards } from "../src/SessionReviewCards";
import type { SessionRecap, SummaryReport } from "../src/api";

// Roadmap 2.19 Review half. The Summary card is where planned-versus-actual lands so it
// follows 10.11's shared range instead of inventing a second date control.

const baseReport = (overrides: Partial<SummaryReport> = {}): SummaryReport => ({
  window: "day",
  generatedAtMs: Date.parse("2026-08-12T12:00:00Z"),
  sessionCount: 1,
  completedSessionCount: 1,
  focusSeconds: 3600,
  sessionLimit: 500,
  sessionsTruncated: false,
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

  it("names the calendar period attendance covers, since the range pill is rolling", () => {
    // "Last 24h" is what the scalar tiles compute; attendance for that preset is measured
    // since local midnight so it can be compared with the daily target. Two periods under one
    // pill is the mismatch the audit flagged -- the tile now says which one it is.
    const { unmount } = render(
      <SummaryCard exportStatus={null} onExport={() => {}} rangeLabel="Last 24h" report={baseReport()} />,
    );
    expect(screen.getByText(/planned \(38%\) · since midnight/)).toBeTruthy();
    unmount();

    render(
      <SummaryCard
        exportStatus={null}
        onExport={() => {}}
        rangeLabel="Last 7 days"
        report={baseReport({ window: "7d", plannedMins: 600 })}
      />,
    );
    expect(screen.getByText(/this calendar week/)).toBeTruthy();
    cleanup();

    // Longer and custom ranges are rolling like everything else: no suffix to add.
    render(
      <SummaryCard
        exportStatus={null}
        onExport={() => {}}
        rangeLabel="Last 30 days"
        report={baseReport({ window: "30d", plannedMins: 0 })}
      />,
    );
    expect(screen.getByText("measured, not scored")).toBeTruthy();
    expect(screen.queryByText(/since midnight|calendar week/)).toBeNull();
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

describe("SummaryCard session time", () => {
  it("shows seconds for a short session and short attended time", () => {
    render(
      <SummaryCard
        exportStatus={null}
        onExport={() => {}}
        rangeLabel="Last 24h"
        report={baseReport({ focusSeconds: 46, attendedSeconds: 46, plannedMins: 0 })}
      />,
    );
    expect(screen.getAllByText("46s")).toHaveLength(2);
    expect(screen.queryByText("0m")).toBeNull();
  });

  it("labels completed-session wall clock as session time, not focus time", () => {
    // The value is started-to-ended of completed sessions, idle and distracted stretches
    // included. Calling it "Focus time" claimed a measurement the model never made.
    render(
      <SummaryCard exportStatus={null} onExport={() => {}} rangeLabel="Today" report={baseReport()} />,
    );
    expect(screen.getByText("Session time")).toBeTruthy();
    expect(screen.queryByText("Focus time")).toBeNull();
    expect(screen.getByText("completed, start to end")).toBeTruthy();
    expect(screen.queryByText(/sessions only/)).toBeNull();
  });

  it("says when the session figures cover only the latest N", () => {
    render(
      <SummaryCard
        exportStatus={null}
        onExport={() => {}}
        rangeLabel="All time"
        report={baseReport({ window: "all", sessionCount: 500, sessionsTruncated: true })}
      />,
    );
    expect(screen.getAllByText(/latest 500 sessions only/)).toHaveLength(2);
  });
});

it("uses the same seconds-aware display in the stopped-session recap", () => {
  const recap: SessionRecap = {
    sessionId: "short",
    goal: "Test",
    durationSecs: 46,
    activeSecs: 46,
    avgFocusScore: 0,
    avgDistractionRisk: 0,
    snapbackCount: 0,
    thrashSpikes: 0,
    deepFocusPct: 0,
  };
  render(
    <SessionReviewCards
      autoLabel={null}
      handleLabel={() => {}}
      handleSkipSurvey={() => {}}
      recap={recap}
      surveyPending={false}
      reflectionPending={false}
      reflectionSaved={false}
      handleSaveReflection={() => {}}
      handleSkipReflection={() => {}}
    />,
  );
  expect(screen.getByText("46s")).toBeInTheDocument();
});
