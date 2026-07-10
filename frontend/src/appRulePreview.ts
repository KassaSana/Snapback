import type { AppRuleKind } from "./api";

/**
 * Patterns at or below this length are flagged as overly broad. Rules match by
 * case-insensitive *substring* against every app name and window title (see
 * `matches_rule_pattern` in the Rust engine), so a 1–2 character pattern like
 * "e" or "in" matches almost everything — a block rule with such a pattern
 * would silently mark nearly every window as distracting.
 */
export const BROAD_APP_RULE_PATTERN_MAX_LEN = 2;

export const isBroadAppRulePattern = (pattern: string) => {
  const trimmed = pattern.trim();
  return trimmed.length > 0 && trimmed.length <= BROAD_APP_RULE_PATTERN_MAX_LEN;
};

export const buildAppRulePreview = (
  pattern: string,
  ruleType: AppRuleKind,
  note?: string,
) => {
  const trimmedPattern = pattern.trim();
  if (!trimmedPattern) {
    return null;
  }

  const basePreview =
    ruleType === "allow"
      ? `Preview: windows matching "${trimmedPattern}" will be treated as on-task.`
      : `Preview: windows matching "${trimmedPattern}" will raise distraction scoring. This does not close or block apps.`;

  const trimmedNote = note?.trim();
  const withNote = trimmedNote
    ? `${basePreview} Note saved with rule: ${trimmedNote}.`
    : basePreview;

  // Guardrail (warn, don't block): a very short pattern matches by substring
  // against every window, so it likely catches far more than intended.
  if (isBroadAppRulePattern(trimmedPattern)) {
    return `${withNote} Heads up: "${trimmedPattern}" is very short and may match many unrelated windows — consider a more specific pattern.`;
  }

  return withNote;
};
