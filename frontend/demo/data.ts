// The dataset behind the hosted demo, all invented. Outside `src/` so the desktop bundle cannot
// include it (nothing it compiles imports it) and it stays out of coverage.

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
  episodes: { sessionId: string; durationSecs: number; returnAppName: string; startedAtMs: number }[];
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
  { app: "Discord", title: "book club — general", file: "" },
  { app: "Chrome", title: "Reddit — r/buildapc", file: "" },
  { app: "Messages", title: "Mum", file: "" },
  { app: "Chrome", title: "Amazon — Your cart", file: "" },
];

const WORK_KINDS: WorkKind[] = [
  {
    goal: "Draft the grant proposal",
    focusMode: "deep",
    centre: 80,
    project: "grant",
    apps: [
      { app: "Word", titles: ["Proposal draft v4.docx", "Budget justification.docx"], onTask: true },
      { app: "Chrome", titles: ["Funder guidelines — PDF"], onTask: true },
    ],
  },
  {
    goal: "Fix the checkout bug",
    focusMode: "deep",
    centre: 76,
    project: "shop-api",
    apps: [
      { app: "Code", titles: ["cart.ts — shop-api", "checkout.test.ts — shop-api"], onTask: true },
      { app: "Windows Terminal", titles: ["npm test — shop-api"], onTask: true },
      { app: "Chrome", titles: ["Stripe docs — PaymentIntents"], onTask: true },
    ],
  },
  {
    goal: "Study for the stats exam",
    focusMode: "normal",
    centre: 66,
    project: "stats",
    apps: [
      { app: "Acrobat", titles: ["Chapter 7 — Regression.pdf"], onTask: true },
      { app: "Notion", titles: ["Stats notes"], onTask: true },
      { app: "Chrome", titles: ["Khan Academy — Confidence intervals"], onTask: true },
    ],
  },
  {
    goal: "Q3 budget review",
    focusMode: "normal",
    centre: 61,
    project: "finance",
    apps: [
      { app: "Excel", titles: ["Q3 budget.xlsx", "Vendor spend.xlsx"], onTask: true },
      { app: "Outlook", titles: ["RE: Q3 numbers"], onTask: true },
    ],
  },
  {
    goal: "Onboarding flow mockups",
    focusMode: "deep",
    centre: 74,
    project: "design",
    apps: [
      { app: "Figma", titles: ["Onboarding — v2", "Components"], onTask: true },
      { app: "Chrome", titles: ["Mobbin — onboarding patterns"], onTask: true },
    ],
  },
  {
    goal: "Read the attention residue paper",
    focusMode: "normal",
    centre: 64,
    project: "reading",
    apps: [
      { app: "Zotero", titles: ["Leroy 2009 — Attention residue"], onTask: true },
      { app: "Obsidian", titles: ["Reading notes"], onTask: true },
    ],
  },
  {
    goal: "Clear the inbox and plan the week",
    focusMode: "recovery",
    centre: 49,
    project: "admin",
    apps: [
      { app: "Outlook", titles: ["Inbox (23)"], onTask: true },
      { app: "Notion", titles: ["Weekly plan"], onTask: true },
    ],
  },
];

const REFLECTIONS: [string, string][] = [
  ["Finished the methods section", "Ask Priya to read it"],
  ["Found the rounding bug in the cart total", "Add a test for zero-quantity items"],
  ["Got through two chapters", "Redo the practice set for ch. 7"],
  ["Most of the variance is in contractors", "Check the September invoices"],
  ["Settled on the three-step version", "Prototype the skip button"],
  ["Took decent notes, lost focus near the end", "Start earlier tomorrow"],
  ["Inbox to zero, week is planned", "Book the dentist"],
  ["Slow start, better after lunch", "Phone in the other room next time"],
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
  const episodes: DemoDataset["episodes"] = [];

  const midnight = new Date(now);
  midnight.setHours(0, 0, 0, 0);
  const todayStart = midnight.getTime();

  let counter = 0;

  for (let dayOffset = 34; dayOffset >= 0; dayOffset -= 1) {
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
      const reflection = REFLECTIONS[Math.floor(random() * REFLECTIONS.length)];

      // Attended time is always less than wall clock.
      const attendedFraction = 0.78 + random() * 0.18;
      const effectiveEnd = isLive ? now : endedAtMs;
      const wallSecs = Math.round((effectiveEnd - startedAtMs) / 1000);

      let snapbackCount = 0;
      let detourStart: number | null = null;
      let sampleIndex = 0;
      let score = kind.centre + (random() - 0.5) * 10;

      for (let t = startedAtMs; t < effectiveEnd; t += 2 * MINUTE + Math.floor(random() * 40) * 1000) {
        sampleIndex += 1;
        // A slow wander around the centre, with occasional dips into distraction.
        const dip = random() < 0.12;
        const pull = (kind.centre - score) * 0.25;
        score = Math.max(8, Math.min(97, score + pull + (random() - 0.5) * 9 - (dip ? 26 : 0)));

        const rounded = Math.round(score);
        const state = focusStateFor(rounded);
        if (state === "DISTRACTED" && detourStart === null) detourStart = t;
        if (state !== "DISTRACTED" && detourStart !== null) {
          episodes.push({
            sessionId,
            durationSecs: Math.round((t - detourStart) / 1000),
            returnAppName: kind.apps[Math.floor(random() * kind.apps.length)].app,
            startedAtMs: detourStart,
          });
          snapbackCount += 1;
          detourStart = null;
        }

        const onTask = state !== "DISTRACTED";
        const source =
          state === "DISTRACTED" && random() < 0.4
            ? "risk"
            : state === "PSEUDO_PRODUCTIVE" && random() < 0.3
              ? "drift"
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
              summary: title,
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
              summary: off.title,
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
  return { sessions, predictions, contexts, episodes };
}
