import type { AppRuleKind } from "./api";

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
  if (!trimmedNote) {
    return basePreview;
  }

  return `${basePreview} Note saved with rule: ${trimmedNote}.`;
};
