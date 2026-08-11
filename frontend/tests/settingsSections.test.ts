import assert from "node:assert/strict";

import {
  DEFAULT_SETTINGS_SECTION,
  SETTINGS_SECTIONS,
  SETTINGS_SECTION_BLURBS,
  SETTINGS_SECTION_LABELS,
  isSettingsSection,
  parseSettingsDeepLink,
  settingsDeepLink,
  settingsHealthBadge,
  settingsPanelId,
  settingsSectionForFailure,
  settingsTabId,
} from "../src/settingsSections";

// THE RULE the item exists for: Settings must not open on developer tooling. Whatever else
// is reordered later, the first section is the ordinary one.
assert.equal(DEFAULT_SETTINGS_SECTION, "general");
assert.equal(SETTINGS_SECTIONS[0], "general");
assert.equal(SETTINGS_SECTIONS[SETTINGS_SECTIONS.length - 1], "advanced");
assert.deepEqual([...SETTINGS_SECTIONS], ["general", "focus", "privacy", "advanced"]);

// Every section is labelled and explained; a group with no blurb is a group whose contents the
// user has to open to identify.
for (const section of SETTINGS_SECTIONS) {
  assert.ok(SETTINGS_SECTION_LABELS[section]?.length > 0, `${section} has a label`);
  assert.ok(SETTINGS_SECTION_BLURBS[section]?.length > 0, `${section} has a blurb`);
  // Ids are unique and distinguishable, since they are wired to aria-controls/labelledby.
  assert.notEqual(settingsTabId(section), settingsPanelId(section));
}
assert.equal(new Set(SETTINGS_SECTIONS.map(settingsTabId)).size, SETTINGS_SECTIONS.length);

// Advanced says "developer" in its own blurb rather than hiding behind a neutral word.
assert.ok(SETTINGS_SECTION_BLURBS.advanced.toLowerCase().includes("developer"));

assert.ok(isSettingsSection("general"));
assert.ok(isSettingsSection("ADVANCED"));
assert.ok(!isSettingsSection("training"));
assert.ok(!isSettingsSection(null));

// ---------------------------------------------------------------------------
// Deep links. Support instructions name a section and must keep working.
// ---------------------------------------------------------------------------

assert.equal(parseSettingsDeepLink("#settings/privacy"), "privacy");
assert.equal(parseSettingsDeepLink("settings/privacy"), "privacy");
assert.equal(parseSettingsDeepLink("#settings/ADVANCED"), "advanced");
// Trailing junk in the path is ignored rather than failing the whole link.
assert.equal(parseSettingsDeepLink("#settings/general/extra"), "general");

// Anything that is not a settings section leaves the caller to keep its current one.
assert.equal(parseSettingsDeepLink("#settings/nonsense"), null);
assert.equal(parseSettingsDeepLink("#review/summary"), null);
assert.equal(parseSettingsDeepLink("#settings"), null);
assert.equal(parseSettingsDeepLink(""), null);
assert.equal(parseSettingsDeepLink(null), null);
assert.equal(parseSettingsDeepLink("#"), null);

// Round-trip: every section's advertised link parses back to itself. This is what stops a
// renamed section from silently breaking the instruction that points at it.
for (const section of SETTINGS_SECTIONS) {
  assert.equal(parseSettingsDeepLink(settingsDeepLink(section)), section);
}

// ---------------------------------------------------------------------------
// Failure routing.
// ---------------------------------------------------------------------------

const failure = (overrides: Partial<Record<string, boolean>> = {}) => ({
  permissionBlocked: false,
  captureFailed: false,
  modelFailed: false,
  ...overrides,
});

// Nothing wrong must not move the user. This is the common case, and an auto-navigation that
// fires on a healthy app is worse than no auto-navigation at all.
assert.equal(settingsSectionForFailure(failure()), null);

assert.equal(settingsSectionForFailure(failure({ permissionBlocked: true })), "privacy");
assert.equal(settingsSectionForFailure(failure({ captureFailed: true })), "privacy");
assert.equal(settingsSectionForFailure(failure({ modelFailed: true })), "advanced");

// Precedence: a blocked capture records nothing, while a failed model still leaves the
// heuristic classifier working. The more severe one wins.
assert.equal(
  settingsSectionForFailure(failure({ permissionBlocked: true, modelFailed: true })),
  "privacy",
);

// ---------------------------------------------------------------------------
// The single health badge that replaced three permanently-exposed header fields.
// ---------------------------------------------------------------------------

{
  const healthy = settingsHealthBadge(failure());
  assert.equal(healthy.warning, false);
  assert.ok(healthy.label.length > 0);

  const blocked = settingsHealthBadge(failure({ permissionBlocked: true }));
  assert.equal(blocked.warning, true);
  assert.equal(blocked.section, "privacy");
  assert.ok(blocked.label.toLowerCase().includes("permit"));

  const stopped = settingsHealthBadge(failure({ captureFailed: true }));
  assert.equal(stopped.warning, true);
  assert.equal(stopped.section, "privacy");
  // A stopped capture and a forbidden one are different problems with different fixes, so
  // they must not share a label.
  assert.notEqual(stopped.label, blocked.label);

  const model = settingsHealthBadge(failure({ modelFailed: true }));
  assert.equal(model.warning, true);
  assert.equal(model.section, "advanced");

  // The badge always names a section to open, including when everything is fine — the link is
  // how a user reaches diagnostics without hunting for them.
  for (const badge of [healthy, blocked, stopped, model]) {
    assert.ok(isSettingsSection(badge.section));
  }
}

console.log("settingsSections.test.ts passed");
