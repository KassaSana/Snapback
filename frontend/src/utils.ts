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

export const formatTime = (isoString: string | null | undefined) => {
  if (!isoString) return "--";
  const date = new Date(isoString);
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

export const nextBackoffDelay = (attempt: number) => {
  const safeAttempt = Math.max(0, attempt);
  const baseMs = 500;
  const maxMs = 10000;
  const delay = baseMs * Math.pow(2, safeAttempt);
  return Math.min(delay, maxMs);
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
