import type { PredictionRecord } from "./api";

export type RiskLevel = "high" | "medium" | "low" | "unknown";

export const clamp = (value: number, min: number, max: number) =>
  Math.min(Math.max(value, min), max);

export const formatPercent = (value: number | null | undefined) => {
  if (value === null || value === undefined || Number.isNaN(value)) return "--";
  const pct = clamp(value, 0, 1) * 100;
  return `${pct.toFixed(1)}%`;
};

export const formatScore = (value: number | null | undefined) => {
  if (value === null || value === undefined || Number.isNaN(value)) return "--";
  const score = clamp(value, 0, 100);
  return score.toFixed(1);
};

// Whole numbers on Now: the score is an opinion on hand-tuned weights, not a measurement.
// Review keeps decimals for comparisons.
export const formatScoreCoarse = (value: number | null | undefined) => {
  if (value === null || value === undefined || Number.isNaN(value)) return "--";
  return String(Math.round(clamp(value, 0, 100)));
};

export const formatPercentCoarse = (value: number | null | undefined) => {
  if (value === null || value === undefined || Number.isNaN(value)) return "--";
  return `${Math.round(clamp(value, 0, 1) * 100)}%`;
};

// Epoch ms to a readable time (ADR-0007). null/undefined means no such moment; 0 is a real
// instant (unconvertible migrated timestamps) and renders as 1970 so it looks wrong.
export const formatTime = (unixMs: number | null | undefined) => {
  if (unixMs === null || unixMs === undefined) return "--";
  const date = new Date(unixMs);
  if (Number.isNaN(date.getTime())) return "--";
  return date.toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
};

export const riskLevel = (risk: number | null | undefined): RiskLevel => {
  if (risk === null || risk === undefined || Number.isNaN(risk)) return "unknown";
  if (risk >= 0.7) return "high";
  if (risk >= 0.4) return "medium";
  return "low";
};

export const riskLabel = (risk: number | null | undefined) => {
  const level = riskLevel(risk);
  if (level === "high") return "High risk";
  if (level === "medium") return "Medium risk";
  if (level === "low") return "Low risk";
  return "Unknown";
};

export const focusStateLabel = (state: string | null | undefined) => {
  switch (state) {
    case "DEEP_FOCUS":
      return "Deep work";
    case "PRODUCTIVE":
      return "Productive";
    case "PSEUDO_PRODUCTIVE":
      return "Drift";
    case "DISTRACTED":
      return "Distracted";
    default:
      return "Unknown";
  }
};

/** Check-in button wording — use this when confirming a session rating the user just chose. */
export const sessionCheckInLabel = (state: string | null | undefined) => {
  switch (state) {
    case "DEEP_FOCUS":
      return "Deep";
    case "PRODUCTIVE":
      return "Focused";
    case "PSEUDO_PRODUCTIVE":
      return "Drift";
    case "DISTRACTED":
      return "Distracted";
    default:
      return focusStateLabel(state);
  }
};

// Colour by the verdict, not the risk (ADR-0004): a Block-rule row at low risk is still
// Distracted.
export const verdictLevel = (state: string | null | undefined): RiskLevel => {
  switch (state) {
    case "DISTRACTED":
      return "high";
    case "PSEUDO_PRODUCTIVE":
      return "medium";
    case "PRODUCTIVE":
    case "DEEP_FOCUS":
      return "low";
    default:
      return "unknown";
  }
};

export const formatPomodoroRemaining = (remainingMs: number) => {
  const totalSeconds = Math.max(0, Math.round(remainingMs / 1000));
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  return `${minutes}:${String(seconds).padStart(2, "0")}`;
};

export const nextBackoffDelay = (attempt: number) => {
  const safeAttempt = Math.max(0, attempt);
  const baseMs = 500;
  const maxMs = 10000;
  const delay = baseMs * Math.pow(2, safeAttempt);
  return Math.min(delay, maxMs);
};

export type VerdictExplanation = {
  reasons: string[];
  caveat: string | null;
  /**
   * True when the model is claiming focus from stillness alone. The Now hero must not
   * print Deep work / Productive in that case — those words are the policy verdict the
   * snapback tracker still uses, but they are unearned as a user-facing claim.
   */
  uncertain: boolean;
};

const GOAL_ALIGNMENT_USED = 0.08;

const isPolicyOverride = (stateSource: string | null | undefined): boolean =>
  stateSource === "block" || stateSource === "risk" || stateSource === "thrash";

/**
 * Quiet + a focus-shaped verdict + nothing else supporting it.
 *
 * `deep_work_score` (classifier.cpp:deep_work_score) is built from time-in-app, low
 * switching, and stability — all measures of *absence*. A quiet screen scores the same
 * whether you are reading a spec or watching a film. Policy overrides are never a guess:
 * a Block rule is evidence the scores cannot show, so those rows keep their verdict word.
 *
 * Display-only. The stored `focus_state` is left alone on purpose (ADR-0004): clamping the
 * score would poison `focus_momentum`, and rewriting the verdict would change snapbacks
 * and streaks to paper over a communication problem.
 */
export const isUncertainFocusGuess = (record: PredictionRecord, goal?: string | null): boolean => {
  if (isPolicyOverride(record.stateSource)) return false;
  const quiet = record.thrashScore <= 0.2 && record.driftScore <= 0.2;
  const claimsFocus = record.focusState === "PRODUCTIVE" || record.focusState === "DEEP_FOCUS";
  if (!quiet || !claimsFocus) return false;
  const goalKnown = Math.abs(record.goalAlignment - 0.5) > GOAL_ALIGNMENT_USED;
  return !(goalKnown && Boolean(goal?.trim()));
};

export type DisplayVerdict = {
  label: string;
  level: RiskLevel;
  uncertain: boolean;
};

/** What the Now hero should print. Waiting and uncertain are not stored states. */
export const displayVerdict = (
  record: PredictionRecord | null,
  goal?: string | null,
): DisplayVerdict => {
  if (!record) {
    return { label: "Waiting for signal", level: "unknown", uncertain: false };
  }
  if (isUncertainFocusGuess(record, goal)) {
    return { label: "Settled", level: "unknown", uncertain: true };
  }
  return {
    label: focusStateLabel(record.focusState),
    level: verdictLevel(record.focusState),
    uncertain: false,
  };
};

// The evidence behind a verdict, in terms of what the classifier actually measured: thrash is
// app switching, drift is title churn plus erratic keystroke intervals.
export const explainPrediction = (
  record: PredictionRecord | null,
  goal?: string | null,
): VerdictExplanation => {
  if (!record) return { reasons: [], caveat: null, uncertain: false };

  const reasons: string[] = [];
  const uncertain = isUncertainFocusGuess(record, goal);

  // When a policy rule decided the verdict, lead with it and suppress the calm-signal phrases.
  const policyReason =
    record.stateSource === "block"
      ? "a blocked app is open"
      : record.stateSource === "risk"
        ? "distraction risk over the mode's bar"
        : null;
  if (policyReason) reasons.push(policyReason);

  if (record.thrashScore >= 0.6) reasons.push("switching apps often");
  else if (record.thrashScore <= 0.2 && !policyReason) reasons.push("no app switching");

  if (record.driftScore >= 0.55) reasons.push("tab and title churn");
  else if (record.driftScore <= 0.2 && !policyReason) reasons.push("settled in one window");

  // 0.5 is the "no goal / no match" default.
  const goalKnown = Math.abs(record.goalAlignment - 0.5) > GOAL_ALIGNMENT_USED;
  const trimmedGoal = goal?.trim();
  if (goalKnown && trimmedGoal) {
    reasons.push(
      record.goalAlignment >= 0.5
        ? `window matches “${trimmedGoal}”`
        : `window is off “${trimmedGoal}”`,
    );
  }

  const caveat = uncertain
    ? "A quiet screen looks the same whether you're working or watching. Tell Snapback if this is wrong."
    : null;

  return { reasons, caveat, uncertain };
};

export const buildSignals = (record: PredictionRecord | null) => {
  if (!record) {
    return ["Waiting for live capture."];
  }

  const level = riskLevel(record.distractionRisk);
  const signals = [
    `Focus state: ${focusStateLabel(record.focusState)}`,
    `Thrash: ${(record.thrashScore * 100).toFixed(0)}% · Drift: ${(record.driftScore * 100).toFixed(0)}% · Goal fit: ${(record.goalAlignment * 100).toFixed(0)}%`,
    `Risk level: ${level}`,
    `Focus score: ${formatScore(record.focusScore)}`,
  ];

  if (record.focusState === "PSEUDO_PRODUCTIVE") {
    signals.push("Drift detected — tab/title churn or scattered typing in a work app.");
  } else if (record.thrashScore >= 0.6) {
    signals.push("Context-switch thrash — jumping between apps/windows rapidly.");
  } else if (record.focusState === "DEEP_FOCUS") {
    signals.push("Deep work detected. Hyperfocus guardrail is watching.");
  } else if (level === "low") {
    signals.push("Focus is stable. Keep momentum.");
  }

  return signals;
};
