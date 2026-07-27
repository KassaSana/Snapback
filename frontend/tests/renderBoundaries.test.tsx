import { memo, useMemo, useState } from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { FocusSummaryCard } from "../src/FocusSummaryCard";
import { PomodoroCard } from "../src/PomodoroCard";
import { SettingsCard } from "../src/SettingsCard";
import type { AutostartStatus, FocusSummary, PomodoroStatus } from "../src/api";
import { useSession } from "../src/useSession";

afterEach(() => cleanup());

describe("prediction render boundaries", () => {
  it("does not re-render stable cards when unrelated parent telemetry changes", () => {
    let nowReads = 0;
    let reviewReads = 0;
    let settingsReads = 0;

    const pomodoroStatus = {
      get running() {
        nowReads += 1;
        return false;
      },
      phase: "work",
      completedWorkIntervals: 0,
      remainingMs: 0,
    } as PomodoroStatus;
    const focusSummary = {
      get sampleCount() {
        reviewReads += 1;
        return 0;
      },
      avgFocusScore: 0,
      peakFocusScore: 0,
      distractedSamples: 0,
      distractedFraction: 0,
      longestFocusStreak: 0,
    } as FocusSummary;
    const autostartStatus = {
      get supported() {
        settingsReads += 1;
        return true;
      },
      enabled: false,
    } as AutostartStatus;
    const onStart = vi.fn();
    const onStop = vi.fn();
    const onAutostartChange = vi.fn();

    function ParentWithTelemetry() {
      const [predictionSequence, setPredictionSequence] = useState(0);
      return (
        <>
          <button onClick={() => setPredictionSequence((value) => value + 1)}>
            Prediction {predictionSequence}
          </button>
          <PomodoroCard
            pomodoroStatus={pomodoroStatus}
            sessionActive
            onStart={onStart}
            onStop={onStop}
          />
          <FocusSummaryCard focusSummary={focusSummary} />
          <SettingsCard
            busy={false}
            error={null}
            onAutostartChange={onAutostartChange}
            status={autostartStatus}
          />
        </>
      );
    }

    render(<ParentWithTelemetry />);
    const readsAfterInitialRender = [nowReads, reviewReads, settingsReads];
    expect(readsAfterInitialRender.every((count) => count > 0)).toBe(true);

    fireEvent.click(screen.getByRole("button", { name: "Prediction 0" }));

    expect(screen.getByRole("button", { name: "Prediction 1" })).toBeInTheDocument();
    expect([nowReads, reviewReads, settingsReads]).toEqual(readsAfterInitialRender);
  });

  it("keeps the session start handler stable across prediction updates", () => {
    let sessionControlRenders = 0;
    const refreshContextTimeline = vi.fn();
    const resetTimelineRefreshGate = vi.fn();
    const setActionError = vi.fn();
    const setLabelStatus = vi.fn();
    const setLabelStatusWarning = vi.fn();

    const StartHandlerProbe = memo(function StartHandlerProbe({
      onStart,
    }: {
      onStart: () => void | Promise<void>;
    }) {
      sessionControlRenders += 1;
      return <span data-start-handler={String(Boolean(onStart))} />;
    });

    function AppLikeSessionHarness() {
      const [predictionSequence, setPredictionSequence] = useState(0);
      const captureReadiness = useMemo(
        () => ({
          captureRunning: true,
          captureFailed: false,
          permissionCaptureAvailable: true,
          activeWindowAvailable: true,
        }),
        [],
      );
      const session = useSession({
        refreshContextTimeline,
        resetTimelineRefreshGate,
        setActionError,
        setLabelStatus,
        setLabelStatusWarning,
        captureReadiness,
      });

      return (
        <>
          <button onClick={() => setPredictionSequence((value) => value + 1)}>
            Session prediction {predictionSequence}
          </button>
          <StartHandlerProbe onStart={session.handleStartSession} />
        </>
      );
    }

    render(<AppLikeSessionHarness />);
    expect(sessionControlRenders).toBe(1);
    fireEvent.click(screen.getByRole("button", { name: "Session prediction 0" }));
    expect(screen.getByRole("button", { name: "Session prediction 1" })).toBeInTheDocument();
    expect(sessionControlRenders).toBe(1);
  });
});
