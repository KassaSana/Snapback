// Roadmap 10.9. The second level of navigation inside Settings.
//
// ADR-0003 fixes the three top-level surfaces, so this adds a level *below* Settings rather
// than a fourth tab. The complaint the item makes is about order, not content: Settings opened
// with model training and permanently exposed classifier backend, model file, and quality state,
// so the product read as an engineering console before it read as a focus tool. Grouping is the
// fix, and the grouping has to be opinionated about which group is first.
//
// Kept pure and separate from the components for the usual reason — the routing rules below are
// the part worth testing, and they are the part that would otherwise only be reachable through
// eight rendered cards.

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
 * Resolve a deep link like `#settings/privacy` to its section.
 *
 * The item requires these to keep working: support instructions say "open Settings → Privacy",
 * and a reorganisation that silently invalidated every such instruction would be a worse
 * outcome than the console it replaced. Returns null when the hash names something else, so a
 * caller can leave the current section alone rather than bouncing the user to General.
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

/** The inverse, so the app can advertise a stable link back to a section. */
export function settingsDeepLink(section: SettingsSection): string {
  return `#settings/${section}`;
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
 * Which section a *real, actionable* failure should reveal — the item's one exception to
 * "Advanced stays collapsed".
 *
 * Two rules make this safe to auto-navigate on. It only fires for failures the user can do
 * something about from the revealed section, and permission problems outrank model problems:
 * a blocked capture means the product is recording nothing, while a failed model load leaves
 * the heuristic classifier working (13.8's whole point). Returns null when nothing is wrong,
 * which is the common case and must not move the user anywhere.
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
