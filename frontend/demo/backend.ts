// The in-memory backend the hosted demo talks to, in place of the C++ engine. Handlers return
// the raw camelCase shapes src/apiMappers.ts expects.
//
//   1. Aggregates are computed from the same generated rows the timeline shows, so Review and
//      Now agree.
//   2. Anything that would touch a real disk (exports, pickers, bundles, training) reports that
//      it is unavailable rather than faking a path.

import {
  buildDataset,
  focusStateFor,
  unit,
  type DemoDataset,
  type DemoPrediction,
  type DemoSession,
} from "./data";

type Json = Record<string, unknown>;
type Range = { window?: string; since?: string; limit?: number };

const MINUTE = 60_000;
const HOUR = 60 * MINUTE;
const DAY = 24 * HOUR;

/** Not a real path. Shown wherever the app would name one, so nobody reads it as their disk. */
const DEMO_PATH_NOTE = "unavailable in the browser demo";

export class DemoBackend {
  private data: DemoDataset;
  private activeSessionId: string | null = null;
  private autoLabels = new Map<string, string>();
  private feedbackLabels: { sessionId: string; label: string; source: string; notes: string | null }[] = [];
  private nextRuleId = 4;
  private counter = 0;

  private settings = {
    defaultFocusMode: "deep",
    idleThresholdSecs: 300,
    pomodoro: {
      workMs: 25 * MINUTE,
      shortBreakMs: 5 * MINUTE,
      longBreakMs: 15 * MINUTE,
      intervalsBeforeLongBreak: 4,
      autoStartNextPhase: true,
    },
    alerts: {
      snapback: ["overlay"],
      hyperfocus: ["native"],
      pomodoro: ["inApp"],
      preview: "detailed",
      quietHoursEnabled: true,
      quietHoursStartMin: 22 * 60,
      quietHoursEndMin: 7 * 60,
      snoozedUntilWallMs: 0,
    } as Json,
  };

  private privacy = {
    privateMode: false,
    excludedApps: ["1Password", "Bitwarden"],
    localOnly: true,
  };

  private goalCategories = [
    { name: "Coding", keywords: ["code", "build", "refactor", "ship", "migration"] },
    { name: "Writing", keywords: ["write", "draft", "doc", "adr", "note"] },
    { name: "Review", keywords: ["review", "pr", "pull request", "feedback"] },
    { name: "Design", keywords: ["design", "figma", "mock", "layout"] },
    { name: "Communication", keywords: ["email", "inbox", "slack", "meeting"] },
  ];

  private rules = [
    { id: 1, pattern: "Code", ruleType: "allow", note: "Editor is always on task" },
    { id: 2, pattern: "YouTube", ruleType: "block", note: null as string | null },
    { id: 3, pattern: "Discord", ruleType: "block", note: null as string | null },
  ];

  private targets = { dailyTargetMins: 240, weeklyTargetMins: 1200 };

  private pomodoro = {
    running: false,
    paused: false,
    awaitingAcknowledgement: false,
    phase: "work",
    completedWorkIntervals: 0,
    remainingMs: 0,
  };

  private pomodoroDeadlineMs = 0;
  // Set while awaiting acknowledgement: the ended phase stays in `pomodoro.phase`, matching
  // native PomodoroTimer; status reports this pending phase to the UI.
  private pomodoroPendingPhase: string | null = null;

  private privatePauseUntil = 0;
  private snoozeUntil = 0;
  private autostart = true;

  private clock: () => number;
  private attendedUpdatedAtMs: number;

  constructor(now: number, clock: () => number = Date.now) {
    this.clock = clock;
    this.attendedUpdatedAtMs = clock();
    this.data = buildDataset(now);
    const live = this.data.sessions.find((s) => s.endedAtMs === null);
    this.activeSessionId = live ? live.sessionId : null;
    this.counter = this.data.sessions.length;
  }

  // --- helpers ------------------------------------------------------------

  private now(): number {
    return this.clock();
  }

  // Attendance excludes private time, including the portion before a timed pause lapses.
  private syncAttendance(): void {
    const now = this.now();
    const session = this.activeSessionId ? this.session(this.activeSessionId) : undefined;
    let from = this.attendedUpdatedAtMs;
    if (this.privacy.privateMode) {
      from = this.privatePauseUntil > 0 ? Math.max(from, this.privatePauseUntil) : now;
    }
    if (session) session.attendedSecs += Math.max(0, now - from) / 1000;
    this.attendedUpdatedAtMs = now;
    this.recordingStatus();
  }

  private resetPomodoro(): void {
    this.pomodoro = { running: false, paused: false, awaitingAcknowledgement: false,
      phase: "work", completedWorkIntervals: 0, remainingMs: 0 };
    this.pomodoroDeadlineMs = 0;
    this.pomodoroPendingPhase = null;
  }

  private pomodoroDuration(phase = this.pomodoro.phase): number {
    const config = this.settings.pomodoro;
    return phase === "work" ? config.workMs
      : phase === "longBreak" ? config.longBreakMs : config.shortBreakMs;
  }

  private nextBreakPhase(completed: number): string {
    const cadence = this.settings.pomodoro.intervalsBeforeLongBreak;
    return cadence > 0 && completed > 0 && completed % cadence === 0 ? "longBreak" : "shortBreak";
  }

  private nextPomodoroPhase(phase = this.pomodoro.phase): string {
    return phase !== "work" ? "work" : this.nextBreakPhase(this.pomodoro.completedWorkIntervals);
  }

  private beginPomodoroPhase(phase = this.pomodoro.phase): void {
    this.pomodoroPendingPhase = null;
    this.pomodoro = {
      ...this.pomodoro,
      phase,
      paused: false,
      awaitingAcknowledgement: false,
      remainingMs: this.pomodoroDuration(phase),
    };
    this.pomodoroDeadlineMs = this.now() + this.pomodoro.remainingMs;
  }

  private pomodoroStatus(): typeof this.pomodoro {
    return {
      ...this.pomodoro,
      phase: this.pomodoro.awaitingAcknowledgement && this.pomodoroPendingPhase
        ? this.pomodoroPendingPhase
        : this.pomodoro.phase,
    };
  }

  private refreshPomodoro(): void {
    if (!this.pomodoro.running || this.pomodoro.paused || this.pomodoro.awaitingAcknowledgement) return;
    const now = this.now();
    while (now >= this.pomodoroDeadlineMs) {
      if (this.pomodoro.phase === "work") this.pomodoro.completedWorkIntervals += 1;
      const next = this.nextPomodoroPhase();
      if (!this.settings.pomodoro.autoStartNextPhase) {
        // Keep the ended phase; the UI reads the pending next phase via pomodoroStatus().
        this.pomodoroPendingPhase = next;
        this.pomodoro = { ...this.pomodoro, awaitingAcknowledgement: true, remainingMs: 0 };
        return;
      }
      this.pomodoro.phase = next;
      this.pomodoroDeadlineMs += this.pomodoroDuration(next);
    }
    this.pomodoro = { ...this.pomodoro, remainingMs: Math.max(0, this.pomodoroDeadlineMs - now) };
  }

  private session(id: string): DemoSession | undefined {
    return this.data.sessions.find((s) => s.sessionId === id);
  }

  /** Resolve a Review window request to a start timestamp. */
  private rangeStart(range: Range | undefined): number {
    const now = this.now();
    const midnight = new Date(now);
    midnight.setHours(0, 0, 0, 0);
    switch (range?.window) {
      case "day":
        return midnight.getTime();
      case "week": {
        const start = new Date(midnight);
        start.setDate(start.getDate() - start.getDay());
        return start.getTime();
      }
      case "7d":
        return now - 7 * DAY;
      case "30d":
        return now - 30 * DAY;
      case "custom":
        return range.since ? Number(new Date(range.since)) || now - 7 * DAY : now - 7 * DAY;
      case "all":
      default:
        return 0;
    }
  }

  private predictionsIn(range: Range | undefined): DemoPrediction[] {
    if (range && range.window === undefined && typeof range.limit === "number") {
      return this.data.predictions.slice(-range.limit);
    }
    const start = this.rangeStart(range);
    return this.data.predictions.filter((p) => p.timestampMs >= start);
  }

  private sessionsIn(range: Range | undefined): DemoSession[] {
    if (range && range.window === undefined && typeof range.limit === "number") {
      return [...this.data.sessions].reverse().slice(0, range.limit);
    }
    const start = this.rangeStart(range);
    return [...this.data.sessions].filter((s) => s.startedAtMs >= start).reverse();
  }

  private latest(): DemoPrediction | null {
    return this.data.predictions.length
      ? this.data.predictions[this.data.predictions.length - 1]
      : null;
  }

  /** Seconds in the longest unbroken non-DISTRACTED run. Samples are two minutes apart. */
  private longestFocusSecs(rows: DemoPrediction[]): number {
    let best = 0;
    let run = 0;
    for (const row of rows) {
      run = row.focusState === "DISTRACTED" ? 0 : run + 120;
      if (run > best) best = run;
    }
    return best;
  }

  private recapOf(session: DemoSession): Json {
    const rows = this.data.predictions.filter((p) => p.sessionId === session.sessionId);
    const end = session.endedAtMs ?? this.now();
    const avg = (pick: (row: DemoPrediction) => number) =>
      rows.length ? rows.reduce((sum, row) => sum + pick(row), 0) / rows.length : 0;
    const deep = rows.filter((r) => r.focusState === "DEEP_FOCUS").length;
    return {
      sessionId: session.sessionId,
      goal: session.goal,
      durationSecs: Math.round((end - session.startedAtMs) / 1000),
      activeSecs: Math.round(session.attendedSecs),
      sampleCount: rows.length,
      avgFocusScore: Math.round(avg((r) => r.focusScore)),
      // Stays on the 0-1 scale: the Review cards run it through formatPercent.
      avgDistractionRisk: unit(avg((r) => r.distractionRisk)),
      snapbackCount: session.snapbackCount,
      thrashSpikes: rows.filter((r) => r.thrashScore > 0.55).length,
      deepFocusPct: rows.length ? Math.round((deep / rows.length) * 100) : 0,
    };
  }

  private sessionJson(session: DemoSession): Json {
    return {
      sessionId: session.sessionId,
      goal: session.goal,
      status: session.status,
      focusMode: session.focusMode,
      startedAtMs: session.startedAtMs,
      endedAtMs: session.endedAtMs,
      reflectionDone: session.reflectionDone,
      reflectionNextStep: session.reflectionNextStep,
    };
  }

  private attendedMinsSince(start: number): number {
    return Math.round(
      this.data.sessions
        .filter((s) => s.startedAtMs >= start)
        .reduce((sum, s) => sum + s.attendedSecs, 0) / 60,
    );
  }

  private recordingStatus(): Json {
    const now = this.now();
    // Mirrors the native lapse: a timed pause whose deadline has passed resumes on
    // read. An indefinite pause (until == 0) never lapses on its own.
    if (this.privacy.privateMode && this.privatePauseUntil > 0 && this.privatePauseUntil <= now) {
      this.privacy.privateMode = false;
      this.privatePauseUntil = 0;
    }
    const privateLeft = Math.max(0, this.privatePauseUntil - now);
    const snoozeLeft = Math.max(0, this.snoozeUntil - now);
    let state = "recording";
    if (this.privacy.privateMode || privateLeft > 0) state = "pausedPrivate";
    else if (!this.activeSessionId) state = "noSession";
    return {
      state,
      privatePauseRemainingMs: privateLeft,
      alertSnoozeRemainingMs: snoozeLeft,
    };
  }

  private health(): Json {
    const latest = this.latest();
    return {
      status: "ok",
      captureRunning: true,
      captureFailed: false,
      captureFailureReason: null,
      overlayFailureReason: null,
      persistenceFailureReason: null,
      captureEventsDropped: 0,
      captureStalled: false,
      // Zeroed: the demo has no engine thread, lock, or SQLite.
      runtime: {
        engineWakeups: 0,
        processCpuMs: 0,
        captureRingHighWater: 0,
        captureRingCapacity: 0,
        storageLockAcquisitions: 0,
        storageLockContended: 0,
        storageLockHoldP50Us: 0,
        storageLockHoldP95Us: 0,
        storageLockMaxHoldUs: 0,
        storageLockWaitP95Us: 0,
        storageLockMaxWaitUs: 0,
        sqliteBusyWaits: 0,
        sqliteBusyExhausted: 0,
        sqliteBusyMaxWaitMs: 0,
      },
      lastPredictionAgeSecs: latest ? Math.round((this.now() - latest.timestampMs) / 1000) : null,
      predictionSuppressionReason: "none",
      permissions: {
        captureAvailable: true,
        captureProbeConfirmed: true,
        activeWindowAvailable: true,
        message: "Sample data — no capture is running in the browser.",
        setupSteps: [],
      },
      classifier: {
        backend: "heuristic",
        onnxRuntimeEnabled: false,
        modelPath: null,
        modelId: "heuristic:snapback-features-v1-31",
        inferenceDegraded: false,
        inferenceFailures: 0,
      },
      modelDeployment: {
        state: "ok",
        message: null,
        preservedPaths: [],
        retryCleanupAvailable: false,
        rollbackAvailable: false,
      },
      developerToolsEnabled: false,
    };
  }

  private unavailable(): Json {
    // Reject rather than resolve: the frontend reads a resolved value as success.
    throw new Error("This writes a file, so it is disabled in the browser demo.");
  }

  /**
   * Advance the live session by one sample and return it, so the Now surface moves.
   *
   * Returns null when nothing is running — the demo should look idle when it is idle rather
   * than inventing activity for a session the visitor already stopped.
   */
  tick(): Json | null {
    this.syncAttendance();
    if (this.recordingStatus().state !== "recording") return null;
    if (!this.activeSessionId) return null;
    const session = this.session(this.activeSessionId);
    if (!session) return null;

    const previous = this.latest();
    const base = previous ? previous.focusScore : 70;
    const drift = (Math.random() - 0.45) * 12;
    const score = Math.max(10, Math.min(96, Math.round(base + drift)));
    const state = focusStateFor(score);
    const onTask = state !== "DISTRACTED";
    const now = this.now();

    const prediction: DemoPrediction = {
      sessionId: session.sessionId,
      focusScore: score,
      distractionRisk: unit((100 - score) / 100),
      focusState: state,
      thrashScore: unit(Math.random() * (onTask ? 0.25 : 0.7)),
      driftScore: unit(Math.random() * (onTask ? 0.2 : 0.65)),
      goalAlignment: onTask ? 0.7 : 0.2,
      timestampMs: now,
      modelId: "heuristic:snapback-features-v1-31",
      stateSource: "model",
    };
    this.data.predictions.push(prediction);
    if (!onTask) session.snapbackCount += 1;
    return prediction as unknown as Json;
  }

  // --- the command table --------------------------------------------------

  handle(command: string, args: Json): unknown {
    this.syncAttendance();
    this.refreshPomodoro();
    const range = args as Range;

    switch (command) {
      case "notify_frontend_ready":
        return null;
      case "get_health":
        return this.health();
      case "report_acceptance_verdict":
        throw new Error("The desktop acceptance harness is unavailable in the browser demo.");
      case "get_diagnostics":
        return {
          version: "demo",
          health: this.health(),
          recentLogs: [
            "demo: dataset generated in the browser",
            "demo: no capture backend is attached",
            `demo: ${this.feedbackLabels.length} feedback submissions retained for this visit`,
            "demo: every number below is derived from the generated rows",
          ],
          supportBundlePrivacyNotice:
            "A real support bundle collects logs from your machine. Nothing is collected here.",
        };

      case "get_latest_prediction":
        return this.latest();
      case "get_prediction_history": {
        const limit = Number(args.limit ?? 8);
        return this.data.predictions.slice(-limit).reverse();
      }

      case "get_focus_summary": {
        const rows = this.predictionsIn(range);
        const distracted = rows.filter((r) => r.focusState === "DISTRACTED").length;
        return {
          sampleCount: rows.length,
          avgFocusScore: rows.length
            ? Math.round(rows.reduce((s, r) => s + r.focusScore, 0) / rows.length)
            : 0,
          peakFocusScore: rows.reduce((peak, r) => Math.max(peak, r.focusScore), 0),
          distractedSamples: distracted,
          distractedFraction: rows.length ? distracted / rows.length : 0,
          longestFocusSecs: this.longestFocusSecs(rows),
        };
      }

      case "get_recording_status":
        return this.recordingStatus();
      case "pause_recording_privately": {
        // 0 means indefinite, matching the native side: an explicit 0 must not fall
        // through to "now plus nothing", which lapses before the next status read.
        const minutes = Number(args.minutes ?? 0);
        this.privacy.privateMode = true;
        this.privatePauseUntil = minutes > 0 ? this.now() + minutes * MINUTE : 0;
        return this.recordingStatus();
      }
      case "resume_recording":
        this.privatePauseUntil = 0;
        this.privacy.privateMode = false;
        return this.recordingStatus();

      case "get_attended_progress":
      case "set_attended_targets": {
        if (command === "set_attended_targets") {
          this.targets = {
            dailyTargetMins: Number(args.dailyMins ?? 0),
            weeklyTargetMins: Number(args.weeklyMins ?? 0),
          };
        }
        const midnight = new Date(this.now());
        midnight.setHours(0, 0, 0, 0);
        const weekStart = new Date(midnight);
        weekStart.setDate(weekStart.getDate() - weekStart.getDay());
        return {
          dailyTargetMins: this.targets.dailyTargetMins,
          dailyActualMins: this.attendedMinsSince(midnight.getTime()),
          weeklyTargetMins: this.targets.weeklyTargetMins,
          weeklyActualMins: this.attendedMinsSince(weekStart.getTime()),
        };
      }

      case "get_pomodoro_status":
        return this.pomodoroStatus();
      case "start_pomodoro":
        if (!this.activeSessionId) throw new Error("Start a session first.");
        this.resetPomodoro();
        this.pomodoro.running = true;
        this.beginPomodoroPhase();
        return this.pomodoroStatus();
      case "stop_pomodoro":
        this.pomodoro = { ...this.pomodoro, running: false, paused: false,
          awaitingAcknowledgement: false, remainingMs: 0 };
        this.pomodoroPendingPhase = null;
        return this.pomodoroStatus();
      case "pause_pomodoro":
        if (this.pomodoro.running && !this.pomodoro.awaitingAcknowledgement) {
          this.pomodoro = { ...this.pomodoro, paused: true };
        }
        return this.pomodoroStatus();
      case "resume_pomodoro":
        if (this.pomodoro.running && this.pomodoro.paused) {
          this.pomodoro = { ...this.pomodoro, paused: false };
          this.pomodoroDeadlineMs = this.now() + this.pomodoro.remainingMs;
        }
        return this.pomodoroStatus();
      case "skip_pomodoro_phase":
        if (this.pomodoro.running) {
          // Awaiting: start the pending phase. Otherwise end early without crediting work.
          const next = this.pomodoro.awaitingAcknowledgement && this.pomodoroPendingPhase
            ? this.pomodoroPendingPhase
            : this.nextPomodoroPhase();
          this.beginPomodoroPhase(next);
        }
        return this.pomodoroStatus();
      case "restart_pomodoro_phase":
        // Restarts the active (or ended-while-awaiting) phase; does not consume the pending one.
        if (this.pomodoro.running) this.beginPomodoroPhase(this.pomodoro.phase);
        return this.pomodoroStatus();
      case "acknowledge_pomodoro_phase":
        if (this.pomodoro.running && this.pomodoro.awaitingAcknowledgement && this.pomodoroPendingPhase) {
          this.beginPomodoroPhase(this.pomodoroPendingPhase);
        }
        return this.pomodoroStatus();
      case "set_pomodoro_config": {
        const config = { ...this.settings.pomodoro, ...((args.config ?? {}) as Json) };
        if (![config.workMs, config.shortBreakMs, config.longBreakMs]
          .every((value) => Number.isFinite(value) && value > 0 && Number.isInteger(value)) ||
          !Number.isInteger(config.intervalsBeforeLongBreak) ||
          config.intervalsBeforeLongBreak < 0) {
          throw new Error(
            "Pomodoro durations must be positive integers; long-break intervals must be nonnegative.",
          );
        }
        this.settings.pomodoro = config;
        return this.pomodoroStatus();
      }

      case "start_session": {
        if (this.activeSessionId) this.handle("stop_session", { sessionId: this.activeSessionId });
        this.resetPomodoro();
        this.counter += 1;
        const session: DemoSession = {
          sessionId: `demo-live-${this.counter}`,
          goal: String(args.goal ?? "Untitled"),
          status: "ACTIVE",
          focusMode: String(args.focusMode ?? this.settings.defaultFocusMode),
          startedAtMs: this.now(),
          endedAtMs: null,
          reflectionDone: null,
          reflectionNextStep: null,
          attendedSecs: 0,
          snapbackCount: 0,
        };
        this.data.sessions.push(session);
        this.activeSessionId = session.sessionId;
        return this.sessionJson(session);
      }
      case "stop_session": {
        const session = this.session(String(args.sessionId));
        if (!session) throw new Error("No such session");
        if (session.endedAtMs !== null) return this.sessionJson(session);
        session.status = "COMPLETED";
        session.endedAtMs = this.now();
        if (this.activeSessionId === session.sessionId) {
          this.activeSessionId = null;
          this.resetPomodoro();
        }
        if (!this.autoLabels.has(session.sessionId) &&
            this.data.predictions.some((row) => row.sessionId === session.sessionId)) {
          const recap = this.recapOf(session);
          const risk = Number(recap.avgDistractionRisk);
          const spikes = Number(recap.thrashSpikes);
          const deep = Number(recap.deepFocusPct);
          this.autoLabels.set(
            session.sessionId,
            deep >= 50 && risk < 0.35
              ? "DEEP_FOCUS"
              : risk >= 0.6 || spikes >= 3
                ? "DISTRACTED"
                : deep < 25 && spikes >= 1
                  ? "PSEUDO_PRODUCTIVE"
                  : "PRODUCTIVE",
          );
        }
        return this.sessionJson(session);
      }
      case "get_session": {
        const session = this.session(String(args.sessionId));
        if (!session) throw new Error("No such session");
        return this.sessionJson(session);
      }
      case "get_active_session": {
        const session = this.activeSessionId ? this.session(this.activeSessionId) : undefined;
        return session ? this.sessionJson(session) : null;
      }
      case "save_session_reflection": {
        const session = this.session(String(args.sessionId));
        if (!session) throw new Error("No such session");
        const trim = (value: unknown) => {
          const text = value == null ? "" : String(value).trim();
          return text.length ? text : null;
        };
        session.reflectionDone = trim(args.done);
        session.reflectionNextStep = trim(args.nextStep);
        return this.sessionJson(session);
      }
      case "get_session_recap": {
        const session = this.session(String(args.sessionId));
        if (!session) throw new Error("No such session");
        return this.recapOf(session);
      }
      case "get_session_auto_label":
        return this.autoLabels.get(String(args.sessionId)) ?? null;
      case "get_session_rating": {
        const id = String(args.sessionId);
        const surveys = this.feedbackLabels.filter((row) => row.sessionId === id && row.source === "survey");
        if (surveys.length > 0) return surveys[surveys.length - 1].label;
        return this.autoLabels.get(id) ?? null;
      }
      case "get_session_focus_curve": {
        // The native slicing (Storage::session_focus_curve), over the demo's rows.
        const id = String(args.sessionId);
        const buckets = Math.min(240, Math.max(0, Number(args.buckets ?? 60)));
        const rows = this.data.predictions
          .filter((p) => p.sessionId === id)
          .sort((a, b) => a.timestampMs - b.timestampMs);
        if (rows.length === 0 || buckets === 0) return [];
        const lo = rows[0].timestampMs;
        const span = rows[rows.length - 1].timestampMs - lo + 1;
        const slices = new Map<number, { startMs: number; sampleCount: number; sum: number }>();
        for (const row of rows) {
          const slice = Math.floor(((row.timestampMs - lo) * buckets) / span);
          const entry = slices.get(slice) ?? { startMs: row.timestampMs, sampleCount: 0, sum: 0 };
          entry.sampleCount += 1;
          entry.sum += row.focusScore;
          slices.set(slice, entry);
        }
        return [...slices.entries()]
          .sort(([a], [b]) => a - b)
          .map(([, s]) => ({
            startMs: s.startMs,
            sampleCount: s.sampleCount,
            avgFocusScore: s.sum / s.sampleCount,
          }));
      }
      case "get_session_longest_snapback": {
        const episodes = this.data.episodes
          .filter((episode) => episode.sessionId === String(args.sessionId))
          .sort((a, b) => b.durationSecs - a.durationSecs || a.startedAtMs - b.startedAtMs);
        return episodes[0] ?? null;
      }
      case "get_session_history":
        return this.sessionsIn(range).map((session) => ({
          record: this.sessionJson(session),
          recap: this.recapOf(session),
        }));
      case "delete_session": {
        const before = this.data.sessions.length;
        const id = String(args.sessionId);
        this.data.sessions = this.data.sessions.filter((s) => s.sessionId !== id);
        this.data.predictions = this.data.predictions.filter((p) => p.sessionId !== id);
        this.data.contexts = this.data.contexts.filter((c) => c.sessionId !== id);
        this.data.episodes = this.data.episodes.filter((episode) => episode.sessionId !== id);
        this.feedbackLabels = this.feedbackLabels.filter((label) => label.sessionId !== id);
        this.autoLabels.delete(id);
        if (this.activeSessionId === id) { this.activeSessionId = null; this.resetPomodoro(); }
        return this.data.sessions.length < before;
      }

      case "get_analytics": {
        const rows = this.predictionsIn(range);
        const start = this.rangeStart(range);
        const hourly = Array.from({ length: 24 }, (_, hour) => {
          const bucket = rows.filter((r) => new Date(r.timestampMs).getHours() === hour);
          const distracted = bucket.filter((r) => r.focusState === "DISTRACTED").length;
          return {
            hour,
            sampleCount: bucket.length,
            avgFocusScore: bucket.length
              ? Math.round(bucket.reduce((s, r) => s + r.focusScore, 0) / bucket.length)
              : 0,
            distractedFraction: bucket.length ? distracted / bucket.length : 0,
          };
        }).filter((row) => row.sampleCount > 0);

        const counts = new Map<string, number>();
        for (const context of this.data.contexts) {
          if (context.timestampMs < start) continue;
          counts.set(context.appName, (counts.get(context.appName) ?? 0) + 1);
        }
        const topApps = [...counts.entries()]
          .sort((a, b) => b[1] - a[1])
          .slice(0, 6)
          .map(([appName, windowCount]) => ({ appName, windowCount }));

        // A "streak" of completed sessions whose average verdict was not distracted.
        let streak = 0;
        for (const session of [...this.data.sessions].reverse()) {
          const recap = this.recapOf(session);
          if (focusStateFor(Number(recap.avgFocusScore)) === "DISTRACTED") break;
          streak += 1;
        }

        return {
          sampleCount: rows.length,
          avgFocusScore: rows.length
            ? Math.round(rows.reduce((s, r) => s + r.focusScore, 0) / rows.length)
            : 0,
          productiveSessionStreak: streak,
          hourly,
          topApps,
        };
      }

      case "get_daily_summary": {
        // Mirrors the native command: one row per local day, ascending, empty days omitted.
        // Focused/deep are run gaps (both endpoints qualify, >120s breaks); demo sessions never
        // cross midnight.
        const windowName = typeof range?.window === "string" ? range.window : "7d";
        const start = this.rangeStart(range);
        const localDay = (ms: number) => {
          const d = new Date(ms);
          const month = String(d.getMonth() + 1).padStart(2, "0");
          const dayOfMonth = String(d.getDate()).padStart(2, "0");
          return `${d.getFullYear()}-${month}-${dayOfMonth}`;
        };
        type DayBucket = {
          day: string;
          attendedSecs: number;
          focusedSecs: number;
          deepFocusSecs: number;
          focusTotal: number;
          sampleCount: number;
          sessionCount: number;
          snapbackCount: number;
        };
        const buckets = new Map<string, DayBucket>();
        const bucketFor = (day: string): DayBucket => {
          let bucket = buckets.get(day);
          if (!bucket) {
            bucket = {
              day,
              attendedSecs: 0,
              focusedSecs: 0,
              deepFocusSecs: 0,
              focusTotal: 0,
              sampleCount: 0,
              sessionCount: 0,
              snapbackCount: 0,
            };
            buckets.set(day, bucket);
          }
          return bucket;
        };

        const rows = this.predictionsIn(range);
        const prevBySession = new Map<string, DemoPrediction>();
        for (const row of rows) {
          const bucket = bucketFor(localDay(row.timestampMs));
          bucket.sampleCount += 1;
          bucket.focusTotal += row.focusScore;
          const prev = prevBySession.get(row.sessionId);
          if (prev) {
            const gapSecs = Math.round((row.timestampMs - prev.timestampMs) / 1000);
            if (gapSecs >= 0 && gapSecs <= 120) {
              if (row.focusState !== "DISTRACTED" && prev.focusState !== "DISTRACTED") {
                bucket.focusedSecs += gapSecs;
              }
              if (row.focusState === "DEEP_FOCUS" && prev.focusState === "DEEP_FOCUS") {
                bucket.deepFocusSecs += gapSecs;
              }
            }
          }
          prevBySession.set(row.sessionId, row);
        }

        for (const session of this.data.sessions) {
          if (session.startedAtMs < start) continue;
          const bucket = bucketFor(localDay(session.startedAtMs));
          bucket.attendedSecs += session.attendedSecs;
          bucket.sessionCount += 1;
          bucket.snapbackCount += session.snapbackCount;
        }

        return {
          window: windowName,
          generatedAtMs: this.now(),
          capped: false,
          days: [...buckets.values()]
            .sort((a, b) => (a.day < b.day ? -1 : 1))
            .map((bucket) => ({
              day: bucket.day,
              attendedSecs: bucket.attendedSecs,
              focusedSecs: bucket.focusedSecs,
              deepFocusSecs: bucket.deepFocusSecs,
              avgFocusScore: bucket.sampleCount
                ? Math.round(bucket.focusTotal / bucket.sampleCount)
                : 0,
              sampleCount: bucket.sampleCount,
              sessionCount: bucket.sessionCount,
              snapbackCount: bucket.snapbackCount,
            })),
        };
      }

      case "get_summary_report": {
        const rows = this.predictionsIn(range);
        const start = this.rangeStart(range);
        const sessions = this.data.sessions.filter((s) => s.startedAtMs >= start);
        const completed = sessions.filter((s) => s.endedAtMs !== null);
        const distracted = rows.filter((r) => r.focusState === "DISTRACTED").length;
        const counts = new Map<string, number>();
        for (const context of this.data.contexts) {
          if (context.timestampMs < start) continue;
          counts.set(context.appName, (counts.get(context.appName) ?? 0) + 1);
        }
        const top = [...counts.entries()].sort((a, b) => b[1] - a[1])[0];
        const windowName = String(range?.window ?? "day");
        const attendedSeconds = sessions.reduce((sum, s) => sum + s.attendedSecs, 0);
        // Session time is completed wall-clock duration, never prediction-row estimates.
        const focusSeconds = completed.reduce((sum, s) => {
          const ended = s.endedAtMs ?? s.startedAtMs;
          return sum + Math.max(0, Math.round((ended - s.startedAtMs) / 1000));
        }, 0);
        // Planned targets only apply to day/week windows, matching native summary_report.
        const plannedMins =
          windowName === "day" || windowName === "today"
            ? this.targets.dailyTargetMins
            : windowName === "week" || windowName === "7d"
              ? this.targets.weeklyTargetMins
              : 0;
        return {
          window: windowName,
          generatedAtMs: this.now(),
          sessionCount: sessions.length,
          completedSessionCount: completed.length,
          focusSeconds,
          sessionLimit: 500,
          sessionsTruncated: false,
          sampleCount: rows.length,
          avgFocusScore: rows.length
            ? Math.round(rows.reduce((s, r) => s + r.focusScore, 0) / rows.length)
            : 0,
          distractedFraction: rows.length ? distracted / rows.length : 0,
          longestFocusSecs: this.longestFocusSecs(rows),
          topContextApp: top ? top[0] : "",
          attendedSeconds,
          plannedMins,
        };
      }

      case "get_context_timeline": {
        const limit = Number(args.limit ?? 20);
        const id = args.sessionId == null ? null : String(args.sessionId);
        const rows = id ? this.data.contexts.filter((c) => c.sessionId === id) : this.data.contexts;
        return rows.slice(-limit).reverse();
      }

      case "get_settings":
        return this.settings as unknown as Json;
      case "set_focus_mode":
        this.settings.defaultFocusMode = String(args.mode ?? "normal");
        return null;
      case "set_idle_threshold":
        this.settings.idleThresholdSecs = Number(args.seconds ?? 300);
        return this.settings as unknown as Json;
      case "set_alert_delivery":
        this.settings.alerts = { ...this.settings.alerts, ...((args.alerts ?? {}) as Json) };
        return this.settings as unknown as Json;
      case "snooze_alerts": {
        // 0 (or absent) means the default 30 minutes, matching the native side.
        const minutes = Number(args.minutes ?? 0);
        this.snoozeUntil = this.now() + (minutes > 0 ? minutes : 30) * MINUTE;
        return this.recordingStatus();
      }
      case "resume_alerts":
        this.snoozeUntil = 0;
        return this.recordingStatus();

      case "get_privacy_settings":
        return this.privacy as unknown as Json;
      case "set_private_mode":
        this.privatePauseUntil = 0;
        this.privacy.privateMode = Boolean(args.enabled);
        return this.privacy as unknown as Json;
      case "set_privacy_exclusions":
        this.privacy.excludedApps = Array.isArray(args.excludedApps)
          ? args.excludedApps.map(String)
          : [];
        return this.privacy as unknown as Json;
      case "delete_all_activity_data":
        this.data.sessions = [];
        this.data.predictions = [];
        this.data.contexts = [];
        this.data.episodes = [];
        this.feedbackLabels = [];
        this.autoLabels.clear();
        this.activeSessionId = null;
        this.resetPomodoro();
        return {
          deleted: ["sessions", "predictions", "context snapshots"],
          failed: [],
          retained: ["your settings", "your rules"],
          complete: true,
        };

      case "get_goal_categories":
        return this.goalCategories;
      case "set_goal_categories":
        this.goalCategories = Array.isArray(args.categories)
          ? (args.categories as Json[]).map((row) => ({
              name: String(row.name ?? ""),
              keywords: Array.isArray(row.keywords) ? row.keywords.map(String) : [],
            }))
          : [];
        return this.goalCategories;

      case "get_app_rules":
        return this.rules;
      case "upsert_app_rule": {
        const request = (args.request ?? {}) as Json;
        const pattern = String(request.pattern ?? "");
        const existing = this.rules.find((rule) => rule.pattern === pattern);
        const now = this.now();
        if (existing) {
          existing.ruleType = String(request.ruleType ?? existing.ruleType);
          existing.note = (request.note ?? null) as string | null;
          return { ...existing, createdAtMs: now, updatedAtMs: now };
        }
        this.nextRuleId += 1;
        const rule = {
          id: this.nextRuleId,
          pattern,
          ruleType: String(request.ruleType ?? "allow"),
          note: (request.note ?? null) as string | null,
        };
        this.rules.push(rule);
        return { ...rule, createdAtMs: now, updatedAtMs: now };
      }
      case "delete_app_rule":
        this.rules = this.rules.filter((rule) => rule.id !== Number(args.id));
        return null;

      case "get_autostart":
        return { enabled: this.autostart, supported: false };
      case "set_autostart":
        this.autostart = Boolean(args.enabled);
        return { enabled: this.autostart, supported: false };

      case "refresh_permissions":
      case "request_permissions":
        return this.health().permissions as Json;

      case "reload_classifier_model":
        return this.health().classifier as Json;
      case "rollback_classifier_model":
        return {
          success: false,
          message: "No trained model exists in the demo.",
          modelId: null,
          classifier: this.health().classifier,
        };
      case "retry_model_deployment_cleanup":
        return this.health().modelDeployment as Json;

      case "get_training_deploy_status":
        return {
          exportDir: DEMO_PATH_NOTE,
          featureCount: 0,
          labelCount: 0,
          labelBreakdown: {},
          hasExport: false,
          modelOnnxExists: false,
          metricsExists: false,
          metrics: null,
          pythonAvailable: false,
          repoPath: null,
          repoConfigured: false,
          pipelineCommand: "training is developer tooling; see ADR-0006",
        };
      case "get_data_import_status":
        return { pending: false };
      case "cancel_data_import":
        return { cancelled: true, pending: false };

      // File-touching commands reject; a resolved fake would read as success.
      case "export_support_bundle":
      case "export_my_data":
      case "export_summary_report":
      case "export_training_data":
      case "open_data_folder":
      case "train_from_export":
      case "cancel_training":
      case "set_training_repo_path":
        return this.unavailable();
      // The UI already renders these refusals faithfully (a dialog that never opened is a
      // cancel).
      case "inspect_data_import":
        return {
          acceptable: false,
          message: "Files cannot be read in the browser demo.",
          schemaVersion: 0,
          sessionCount: 0,
        };
      case "stage_data_import":
        return {
          ok: false,
          message: "Files cannot be staged in the browser demo.",
          schemaVersion: 0,
          sessionCount: 0,
        };
      case "pick_open_file":
      case "pick_save_file":
        return {
          ok: false,
          cancelled: true,
          path: "",
          message: "File pickers are disabled in the browser demo.",
        };

      case "submit_label": {
        const request = (args.request ?? {}) as Json;
        const sessionId = String(request.sessionId ?? "");
        const session = this.session(sessionId);
        if (!session) throw new Error("No such session");
        const label = String(request.label ?? "");
        const source = String(request.source ?? "manual");
        if (!["DEEP_FOCUS", "PRODUCTIVE", "PSEUDO_PRODUCTIVE", "DISTRACTED"].includes(label))
          throw new Error("Unknown focus label");
        if (!["manual", "hotkey", "survey", "auto"].includes(source))
          throw new Error("Unknown label source");
        if ((source === "manual" || source === "hotkey") && this.activeSessionId !== sessionId)
          throw new Error("Start a session to save live feedback.");
        this.feedbackLabels.push({ sessionId, label, source, notes: request.notes == null ? null : String(request.notes) });
        return null;
      }
      case "dismiss_snapback":
      case "dismiss_untracked_nudge":
        return null;
      case "restore_snapback_target":
        return {
          ok: false,
          message: "Raising another application's window needs the desktop app.",
        };

      default:
        throw new Error(`The demo does not implement "${command}"`);
    }
  }
}
