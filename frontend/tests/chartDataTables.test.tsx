import { cleanup, fireEvent, render, screen, within } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import { AnalyticsCard } from "../src/AnalyticsCard";
import type { AnalyticsSummary, DailySummary, SessionSummary } from "../src/api";
import { DailyTrendCard } from "../src/DailyTrendCard";
import { localIsoDay } from "../src/dailyTrendChart";
import { InsightsCard } from "../src/InsightsCard";

// Roadmap 10.3. Each Review chart is `<svg role="img">`, which hides its per-bar titles from
// assistive tech. These pin the table that carries the same numbers: one reachable control,
// a real table, and values that match what the bars are drawn from.

afterEach(() => {
  cleanup();
});

const openData = (card: HTMLElement) => {
  const summary = within(card).getByText("Show data");
  fireEvent.click(summary);
  return within(card).getByRole("table");
};

describe("Review chart data tables", () => {
  it("lists focus by hour, with the no-data hours named as such", () => {
    const analytics: AnalyticsSummary = {
      sampleCount: 40,
      avgFocusScore: 71,
      productiveSessionStreak: 0,
      hourly: [{ hour: 9, sampleCount: 40, avgFocusScore: 71.4, distractedFraction: 0.25 }],
      topApps: [],
    };
    const { container } = render(<AnalyticsCard analytics={analytics} rangeLabel="Last 7 days" />);
    const table = openData(container);

    // Twenty-four rows: the table has the chart's shape, empty hours included.
    expect(within(table).getAllByRole("row")).toHaveLength(1 + 24);
    const nine = within(table).getByRole("rowheader", { name: "09:00" });
    expect(nine.closest("tr")).toHaveTextContent("focus 71 of 100 · 40 samples · 25% distracted");
    expect(within(table).getByRole("rowheader", { name: "10:00" }).closest("tr")).toHaveTextContent(
      "no data",
    );
    expect(screen.getByRole("img")).toHaveAccessibleName(/data table below/);
  });

  it("lists deep work per day", () => {
    const today = localIsoDay();
    const dailySummary: DailySummary = {
      window: "7d",
      generatedAtMs: Date.now(),
      capped: false,
      days: [
        {
          day: today,
          attendedSecs: 7200,
          focusedSecs: 5400,
          deepFocusSecs: 3600,
          avgFocusScore: 80,
          sampleCount: 7200,
          sessionCount: 1,
          snapbackCount: 0,
        },
      ],
    };
    const { container } = render(
      <DailyTrendCard dailySummary={dailySummary} rangePreset="7d" rangeLabel="Last 7 days" />,
    );
    const table = openData(container);

    expect(within(table).getAllByRole("row").length).toBeGreaterThan(1);
    expect(
      within(table).getByText(/attended 2h · deep 1h · avg focus 80/),
    ).toBeInTheDocument();
  });

  it("lists focus per session by goal", () => {
    const session = (id: string, goal: string, score: number, startedAtMs: number) =>
      ({
        record: {
          sessionId: id,
          goal,
          status: "COMPLETED",
          focusMode: "normal",
          startedAtMs,
          endedAtMs: startedAtMs + 3_600_000,
          reflectionDone: null,
          reflectionNextStep: null,
        },
        recap: { sessionId: id, goal, durationSecs: 3600, activeSecs: 3600, avgFocusScore: score },
      }) as unknown as SessionSummary;
    const { container } = render(
      <InsightsCard
        rangeLabel="Last 7 days"
        sessionHistory={[
          session("b", "Review the PR", 64.6, 2_000_000),
          session("a", "Write tests", 82, 1_000_000),
        ]}
      />,
    );
    const table = openData(container);

    expect(
      within(table).getByRole("rowheader", { name: "Write tests" }).closest("tr"),
    ).toHaveTextContent("focus 82");
    expect(
      within(table).getByRole("rowheader", { name: "Review the PR" }).closest("tr"),
    ).toHaveTextContent("focus 65");
  });
});
