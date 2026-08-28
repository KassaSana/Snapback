// The dataset behind the hosted demo.
//
// This directory is deliberately outside `src/`. The demo is a *second* entry point
// (`demo.html`), never imported by `src/main.tsx`, so there is no build flag to get wrong and
// no tree-shaking to trust: the desktop bundle cannot contain this code because nothing it
// compiles refers to it. Being outside `src/` also keeps it out of the Vitest coverage
// denominator without touching the exclusion list that `check_coverage_exclusions.py` guards.
//
// Everything here is invented. No real capture data is committed to this repository.

/** Deterministic PRNG, so the demo tells the same story to every visitor. */
function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const MINUTE = 60_000;
const HOUR = 60 * MINUTE;
const DAY = 24 * HOUR;

export const FOCUS_STATES = [
  "DISTRACTED",
  "PSEUDO_PRODUCTIVE",
  "PRODUCTIVE",
  "DEEP_FOCUS",
] as const;

/**
 * The scales are not uniform, and getting one wrong is silently wrong rather than broken.
 * `focusScore` is 0-100; `distractionRisk`, `thrashScore`, `driftScore`, and `goalAlignment`
 * are all 0-1 and are multiplied by 100 at display time (`formatPercent` in `src/utils.ts`).
 */
export const unit = (value: number): number => Number(Math.max(0, Math.min(1, value)).toFixed(2));

/** The verdict a score implies. Mirrors the thresholds the real classifier policy uses. */
export function focusStateFor(score: number): string {
  if (score >= 80) return "DEEP_FOCUS";
  if (score >= 60) return "PRODUCTIVE";
  if (score >= 40) return "PSEUDO_PRODUCTIVE";
  return "DISTRACTED";
}

export type DemoContext = {
  appName: string;
  windowTitle: string;
  fileHint: string;
  projectHint: string;
  summary: string;
  timestampMs: number;
  sessionId: string;
};

export type DemoPrediction = {
  sessionId: string;
  focusScore: number;
  distractionRisk: number;
  focusState: string;
  thrashScore: number;
  driftScore: number;
  goalAlignment: number;
  timestampMs: number;
  modelId: string;
  stateSource: string | null;
};

export type DemoSession = {
  sessionId: string;
  goal: string;
  status: string;
  focusMode: string;
  startedAtMs: number;
  endedAtMs: number | null;
  reflectionDone: string | null;
  reflectionNextStep: string | null;
  attendedSecs: number;
  snapbackCount: number;
};

export type DemoDataset = {
  sessions: DemoSession[];
  predictions: DemoPrediction[];
  contexts: DemoContext[];
};

type WorkKind = {
  goal: string;
  focusMode: string;
  /** Mean focus score for this kind of work; the walk wanders around it. */
  centre: number;
  apps: { app: string; titles: string[]; onTask: boolean }[];
  project: string;
};

const DISTRACTIONS = [
  { app: "Chrome", title: "Hacker News", file: "" },
  { app: "Slack", title: "#general — 3 unread", file: "" },
  { app: "Chrome", title: "YouTube — Watch Later", file: "" },
  { app: "Discord", title: "gamedev — general", file: "" },
];

const WORK_KINDS: WorkKind[] = [
  {
    goal: "Ship the storage migration",
    focusMode: "deep",
    centre: 82,
    project: "snapback",
    apps: [
      {
        app: "Code",
        titles: ["storage.cpp — snapback", "storage.hpp — snapback", "migrations.md — snapback"],
        onTask: true,
      },
      { app: "Windows Terminal", titles: ["ctest — snapback"], onTask: true },
      { app: "Chrome", titles: ["SQLite — ALTER TABLE"], onTask: true },
    ],
  },
  {
    goal: "Write the architecture decision record",
    focusMode: "normal",
    centre: 71,
    project: "docs",
    apps: [
      { app: "Code", titles: ["0008-protect-master.md — docs", "ROADMAP.md — docs"], onTask: true },
      { app: "Chrome", titles: ["GitHub — branch protection docs"], onTask: true },
    ],
  },
  {
    goal: "Review the pull request queue",
    focusMode: "normal",
    centre: 58,
    project: "review",
    apps: [
      { app: "Chrome", titles: ["Pull requests — Snapback", "Files changed — #48"], onTask: true },
      { app: "Slack", titles: ["#snapback-dev"], onTask: true },
    ],
  },
  {
    goal: "Redesign the Review surface",
    focusMode: "deep",
    centre: 77,
    project: "frontend",
    apps: [
      { app: "Figma", titles: ["Review — charts v3"], onTask: true },
      { app: "Code", titles: ["AnalyticsCard.tsx — frontend"], onTask: true },
    ],
  },
  {
    goal: "Clear the inbox and plan the week",
    focusMode: "recovery",
    centre: 49,
    project: "admin",
    apps: [
      { app: "Notion", titles: ["Weekly plan"], onTask: true },
      { app: "Chrome", titles: ["Mail — Inbox (12)"], onTask: true },
    ],
  },
];

const REFLECTIONS: [string, string][] = [
  ["Landed the migration and its backup path", "Write the downgrade test"],
  ["Drafted the ADR end to end", "Get the required-check list confirmed"],
  ["Reviewed four PRs", "Follow up on the flaky sanitizer case"],
  ["Charts now read honestly at zero data", "Pick the empty-state copy"],
];

function pad(value: number): string {
  return String(value).padStart(2, "0");
}

/**
 * Build the whole dataset relative to `now`, so the demo always looks like it was recorded
 * today rather than on the day it happened to be authored.
 */
export function buildDataset(now: number, seed = 20260827): DemoDataset {
  const random = mulberry32(seed);
  const sessions: DemoSession[] = [];
  const predictions: DemoPrediction[] = [];
  const contexts: DemoContext[] = [];

  const midnight = new Date(now);
  midnight.setHours(0, 0, 0, 0);
  const todayStart = midnight.getTime();

  let counter = 0;

  for (let dayOffset = 13; dayOffset >= 0; dayOffset -= 1) {
    const dayStart = todayStart - dayOffset * DAY;
    const weekday = new Date(dayStart).getDay();
    const isWeekend = weekday === 0 || weekday === 6;
    const sessionCount = isWeekend ? 1 : 2 + Math.floor(random() * 2);

    let cursor = dayStart + 9 * HOUR + Math.floor(random() * 45) * MINUTE;

    for (let index = 0; index < sessionCount; index += 1) {
      const kind = WORK_KINDS[Math.floor(random() * WORK_KINDS.length)];
      const durationMins = 30 + Math.floor(random() * 75);
      const startedAtMs = cursor;
      const endedAtMs = startedAtMs + durationMins * MINUTE;

      // A live session only exists today, and only if it would still be running.
      const isLive = dayOffset === 0 && endedAtMs > now;
      if (startedAtMs > now) break;

      counter += 1;
      const sessionId = `demo-${pad(counter)}`;
      const reflection = REFLECTIONS[counter % REFLECTIONS.length];

      // Attended time is always less than wall clock: that gap is the point of 7.23.
      const attendedFraction = 0.78 + random() * 0.18;
      const effectiveEnd = isLive ? now : endedAtMs;
      const wallSecs = Math.round((effectiveEnd - startedAtMs) / 1000);

      let snapbackCount = 0;
      let sampleIndex = 0;
      let score = kind.centre + (random() - 0.5) * 10;

      for (let t = startedAtMs; t < effectiveEnd; t += 2 * MINUTE) {
        sampleIndex += 1;
        // A slow wander around the centre, with occasional dips into distraction.
        const dip = random() < 0.12;
        const pull = (kind.centre - score) * 0.25;
        score = Math.max(8, Math.min(97, score + pull + (random() - 0.5) * 9 - (dip ? 26 : 0)));

        const rounded = Math.round(score);
        const state = focusStateFor(rounded);
        if (state === "DISTRACTED") snapbackCount += 1;

        const onTask = state !== "DISTRACTED";
        const source =
          state === "DISTRACTED" && random() < 0.4
            ? "risk"
            : state === "DEEP_FOCUS" && random() < 0.2
              ? "thrash"
              : "model";

        predictions.push({
          sessionId,
          focusScore: rounded,
          distractionRisk: unit((100 - rounded + (random() - 0.5) * 12) / 100),
          focusState: state,
          thrashScore: unit(random() * (onTask ? 0.25 : 0.7)),
          driftScore: unit(random() * (onTask ? 0.2 : 0.65)),
          goalAlignment: unit(onTask ? 0.62 + random() * 0.33 : 0.08 + random() * 0.3),
          timestampMs: t,
          modelId: "heuristic:snapback-features-v1-31",
          stateSource: source,
        });

        // One context row every third sample, so the timeline is readable rather than dense.
        if (sampleIndex % 3 === 1) {
          if (onTask) {
            const choice = kind.apps[Math.floor(random() * kind.apps.length)];
            const title = choice.titles[Math.floor(random() * choice.titles.length)];
            const file = title.includes(" — ") ? title.split(" — ")[0] : "";
            contexts.push({
              appName: choice.app,
              windowTitle: title,
              fileHint: file,
              projectHint: kind.project,
              summary: `${choice.app} · ${title}`,
              timestampMs: t,
              sessionId,
            });
          } else {
            const off = DISTRACTIONS[Math.floor(random() * DISTRACTIONS.length)];
            contexts.push({
              appName: off.app,
              windowTitle: off.title,
              fileHint: off.file,
              projectHint: "",
              summary: `${off.app} · ${off.title}`,
              timestampMs: t,
              sessionId,
            });
          }
        }
      }

      sessions.push({
        sessionId,
        goal: kind.goal,
        status: isLive ? "ACTIVE" : "COMPLETED",
        focusMode: kind.focusMode,
        startedAtMs,
        endedAtMs: isLive ? null : endedAtMs,
        reflectionDone: isLive ? null : reflection[0],
        reflectionNextStep: isLive ? null : reflection[1],
        attendedSecs: Math.round(wallSecs * attendedFraction),
        snapbackCount,
      });

      cursor = endedAtMs + (25 + Math.floor(random() * 90)) * MINUTE;
      if (cursor > dayStart + 19 * HOUR) break;
    }
  }

  predictions.sort((a, b) => a.timestampMs - b.timestampMs);
  contexts.sort((a, b) => a.timestampMs - b.timestampMs);
  return { sessions, predictions, contexts };
}
