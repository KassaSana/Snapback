import { fireEvent, render, screen } from "@testing-library/react";

import App from "../src/App";
import type { Surface } from "../src/SurfaceNav";
import {
  DEFAULT_SETTINGS_SECTION,
  SETTINGS_SECTION_LABELS,
  type SettingsSection,
} from "../src/settingsSections";

// Cards mount only when their surface (ADR-0003) and Settings group are showing, so tests must
// name them. Defaults to "now", matching launch.
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
