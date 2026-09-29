// The second level of navigation inside Settings (ADR-0003 fixes the top three surfaces).
// General comes first; model tooling is last.

export const SETTINGS_SECTIONS = ["general", "focus", "privacy", "advanced"] as const;
export type SettingsSection = (typeof SETTINGS_SECTIONS)[number];

export const DEFAULT_SETTINGS_SECTION: SettingsSection = "general";

export const SETTINGS_SECTION_LABELS: Record<SettingsSection, string> = {
  general: "General",
  focus: "Focus",
  privacy: "Privacy & permissions",
  advanced: "Advanced",
};

/**
 * What each group is for, shown under its heading.
 *
 * The Advanced blurb says "developer" outright. A section that hides model training behind a
 * neutral word is how the console creeps back: the user should be able to tell from the label
 * alone that nothing in here is required to use the product.
 */
export const SETTINGS_SECTION_BLURBS: Record<SettingsSection, string> = {
  general: "How Snapback starts and when it counts you as away.",
  focus: "What counts as focused work, and the rules that correct it.",
  privacy: "What is recorded, what leaves this machine, and what the OS has allowed.",
  advanced: "Developer tooling and raw diagnostics. Nothing here is needed for normal use.",
};

export function settingsTabId(section: SettingsSection): string {
  return `settings-tab-${section}`;
}

export function settingsPanelId(section: SettingsSection): string {
  return `settings-panel-${section}`;
}

export function isSettingsSection(value: unknown): value is SettingsSection {
  return SETTINGS_SECTIONS.includes(String(value ?? "").toLowerCase() as SettingsSection);
}

/**
 * Resolve a deep link like `#settings/privacy` to its section, so support instructions keep
 * working. null for anything else, so the caller leaves the current section alone.
 */
export function parseSettingsDeepLink(hash: string | null | undefined): SettingsSection | null {
  const raw = String(hash ?? "").trim().replace(/^#/, "");
  if (!raw) return null;
  const parts = raw.split("/").filter(Boolean);
  if (parts.length < 2) return null;
  if (parts[0].toLowerCase() !== "settings") return null;
  const section = parts[1].toLowerCase();
  return isSettingsSection(section) ? (section as SettingsSection) : null;
}

export type SettingsFailureInput = {
  /** The OS refused capture, or the capability probe failed. */
  permissionBlocked: boolean;
  /** Capture died or stalled while a session was recording. */
  captureFailed: boolean;
  /** A model load/deploy went wrong. Developer-only under ADR-0006. */
  modelFailed: boolean;
};

/**
 * Which section a real, actionable failure should reveal. Permission problems outrank model
 * problems (a failed model load leaves the heuristic working). null when nothing is wrong.
 */
export function settingsSectionForFailure(
  input: SettingsFailureInput,
): SettingsSection | null {
  if (input.permissionBlocked || input.captureFailed) return "privacy";
  if (input.modelFailed) return "advanced";
  return null;
}

export type HealthBadge = {
  label: string;
  /** True when the badge is reporting a problem rather than an all-clear. */
  warning: boolean;
  /** The section its "technical details" link opens. */
  section: SettingsSection;
};

/**
 * The single compact health badge that replaces the permanently-exposed classifier backend,
 * model path, and quality state in the global header.
 *
 * One badge, not three fields: the header's job is to say whether anything needs attention,
 * and the answer is one bit. Everything behind it is still one click away, which is the
 * difference between hiding information and ordering it.
 */
export function settingsHealthBadge(input: SettingsFailureInput): HealthBadge {
  const section = settingsSectionForFailure(input);
  if (!section) {
    return { label: "All systems normal", warning: false, section: "advanced" };
  }
  if (section === "privacy") {
    return {
      label: input.permissionBlocked ? "Capture not permitted" : "Capture stopped",
      warning: true,
      section,
    };
  }
  return { label: "Model unavailable", warning: true, section };
}
