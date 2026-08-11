import { fireEvent, render, screen } from "@testing-library/react";

import App from "../src/App";
import type { Surface } from "../src/SurfaceNav";
import {
  DEFAULT_SETTINGS_SECTION,
  SETTINGS_SECTION_LABELS,
  type SettingsSection,
} from "../src/settingsSections";

// ADR-0003 split the dashboard into Now / Review / Settings, so a card is only mounted
// when its surface is showing. Tests that assert on a Review or Settings card must say so
// — that is the behaviour, not an inconvenience: the old tests passed because everything
// was rendered at once, which was the problem the ADR fixed.
//
// Roadmap 10.9 added a second level inside Settings for the same reason: General, Focus,
// Privacy & permissions, and Advanced each mount only when selected, so a Settings test now
// has to name its group. Same principle as the ADR — the grouping *is* the behaviour, and a
// test that finds a privacy control while sitting on General would be asserting the flat
// stream this item removed.
//
// Defaults to "now" so `renderApp()` matches what a user sees on launch.
export function renderApp(
  surface: Surface = "now",
  section: SettingsSection = DEFAULT_SETTINGS_SECTION,
) {
  const result = render(<App />);
  if (surface !== "now") {
    // Role-based, so this breaks loudly if the nav stops being a real tablist.
    fireEvent.click(screen.getByRole("tab", { name: surfaceLabel(surface) }));
  }
  if (surface === "settings" && section !== DEFAULT_SETTINGS_SECTION) {
    fireEvent.click(screen.getByRole("tab", { name: SETTINGS_SECTION_LABELS[section] }));
  }
  return result;
}

function surfaceLabel(surface: Surface): string {
  return surface === "review" ? "Review" : "Settings";
}
