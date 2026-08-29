// The in-memory backend the hosted demo talks to, in place of the C++ engine.
//
// Every handler returns the *raw* shape `src/apiMappers.ts` expects. The mappers accept
// camelCase as readily as the native side's snake_case, so these read as plain objects.
//
// Two rules this file follows, because a demo that lies is worse than no demo:
//
//   1. Aggregates are computed from the same generated rows the timeline shows. Review and Now
//      cannot disagree, which is the exact class of bug 10.13 and 10.11 were opened for.
//   2. Anything that would touch a real disk (exports, file pickers, support bundles, training)
//      reports honestly that it is unavailable here rather than faking a file path.

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
    completedWorkIntervals: 2,
    remainingMs: 25 * MINUTE,
  };

  private privatePauseUntil = 0;
  private snoozeUntil = 0;
  private autostart = true;

  constructor(now: number) {
    this.data = buildDataset(now);
    const live = this.data.sessions.find((s) => s.endedAtMs === null);
    this.activeSessionId = live ? live.sessionId : null;
    this.counter = this.data.sessions.length;
  }

  // --- helpers ------------------------------------------------------------

  private now(): number {
    return Date.now();
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
      activeSecs: session.attendedSecs,
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

  private unavailable(extra: Json = {}): Json {
    return {
      ok: false,
      cancelled: true,
      supported: false,
      opened: false,
      path: DEMO_PATH_NOTE,
      outputPath: DEMO_PATH_NOTE,
      message: "This writes a file, so it is disabled in the browser demo.",
      ...extra,
    };
  }

  /**
   * Advance the live session by one sample and return it, so the Now surface moves.
   *
   * Returns null when nothing is running — the demo should look idle when it is idle rather
   * than inventing activity for a session the visitor already stopped.
   */
  tick(): Json | null {
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
    session.attendedSecs += 5;
    if (!onTask) session.snapbackCount += 1;
    return prediction as unknown as Json;
  }

  // --- the command table --------------------------------------------------

  handle(command: string, args: Json): unknown {
    const range = args as Range;

    switch (command) {
      case "get_health":
        return this.health();
      case "get_diagnostics":
        return {
          version: "demo",
          health: this.health(),
          recentLogs: [
            "demo: dataset generated in the browser",
            "demo: no capture backend is attached",
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
      case "pause_recording_privately":
        this.privatePauseUntil = this.now() + Number(args.minutes ?? 30) * MINUTE;
        return this.recordingStatus();
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
        return this.pomodoro;
      case "start_pomodoro":
        this.pomodoro = {
          ...this.pomodoro,
          running: true,
          paused: false,
          remainingMs: 25 * MINUTE,
        };
        return this.pomodoro;
      case "stop_pomodoro":
        this.pomodoro = { ...this.pomodoro, running: false, paused: false };
        return this.pomodoro;
      case "pause_pomodoro":
        this.pomodoro = { ...this.pomodoro, paused: true };
        return this.pomodoro;
      case "resume_pomodoro":
        this.pomodoro = { ...this.pomodoro, paused: false };
        return this.pomodoro;
      case "skip_pomodoro_phase":
      case "restart_pomodoro_phase":
      case "acknowledge_pomodoro_phase":
        this.pomodoro = {
          ...this.pomodoro,
          awaitingAcknowledgement: false,
          remainingMs: this.pomodoro.phase === "work" ? 25 * MINUTE : 5 * MINUTE,
        };
        return this.pomodoro;
      case "set_pomodoro_config": {
        const config = (args.config ?? {}) as Json;
        this.settings.pomodoro = {
          ...this.settings.pomodoro,
          ...config,
        } as typeof this.settings.pomodoro;
        return this.pomodoro;
      }

      case "start_session": {
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
        session.status = "COMPLETED";
        session.endedAtMs = this.now();
        session.attendedSecs = Math.round((session.endedAtMs - session.startedAtMs) / 1000);
        if (this.activeSessionId === session.sessionId) this.activeSessionId = null;
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

      case "get_summary_report": {
        const rows = this.predictionsIn(range);
        const start = this.rangeStart(range);
        const sessions = this.data.sessions.filter((s) => s.startedAtMs >= start);
        const distracted = rows.filter((r) => r.focusState === "DISTRACTED").length;
        const counts = new Map<string, number>();
        for (const context of this.data.contexts) {
          if (context.timestampMs < start) continue;
          counts.set(context.appName, (counts.get(context.appName) ?? 0) + 1);
        }
        const top = [...counts.entries()].sort((a, b) => b[1] - a[1])[0];
        const windowName = String(range?.window ?? "day");
        const attendedSeconds = sessions.reduce((sum, s) => sum + s.attendedSecs, 0);
        const days = Math.max(1, Math.round((this.now() - start) / DAY));
        return {
          window: windowName,
          generatedAtMs: this.now(),
          sessionCount: sessions.length,
          completedSessionCount: sessions.filter((s) => s.endedAtMs !== null).length,
          focusSeconds: rows.filter((r) => r.focusState !== "DISTRACTED").length * 120,
          sampleCount: rows.length,
          avgFocusScore: rows.length
            ? Math.round(rows.reduce((s, r) => s + r.focusScore, 0) / rows.length)
            : 0,
          distractedFraction: rows.length ? distracted / rows.length : 0,
          longestFocusSecs: this.longestFocusSecs(rows),
          topContextApp: top ? top[0] : "",
          attendedSeconds,
          plannedMins:
            windowName === "day"
              ? this.targets.dailyTargetMins
              : days * this.targets.dailyTargetMins,
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
      case "snooze_alerts":
        this.snoozeUntil = this.now() + Number(args.minutes ?? 30) * MINUTE;
        return this.recordingStatus();
      case "resume_alerts":
        this.snoozeUntil = 0;
        return this.recordingStatus();

      case "get_privacy_settings":
        return this.privacy as unknown as Json;
      case "set_private_mode":
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
        this.activeSessionId = null;
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

      // Everything that would write to, read from, or open a real file.
      case "export_support_bundle":
      case "export_my_data":
      case "export_summary_report":
      case "export_training_data":
      case "open_data_folder":
      case "pick_open_file":
      case "pick_save_file":
      case "inspect_data_import":
      case "stage_data_import":
      case "train_from_export":
      case "set_training_repo_path":
        return this.unavailable();

      case "submit_label":
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
