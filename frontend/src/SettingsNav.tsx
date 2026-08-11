// Roadmap 10.9. The second-level nav inside Settings.
//
// A tablist for the same reason SurfaceNav is one: the panel below is swapped in place, which
// is what `role="tab"` means, and keyboard users get arrow-key traversal rather than eight
// cards of tab stops between them and the group they wanted.

import { memo } from "react";

import {
  SETTINGS_SECTIONS,
  SETTINGS_SECTION_LABELS,
  settingsPanelId,
  settingsTabId,
  type SettingsSection,
} from "./settingsSections";

type Props = {
  active: SettingsSection;
  onChange: (section: SettingsSection) => void;
};

export const SettingsNav = memo(function SettingsNav({ active, onChange }: Props) {
  const handleKeyDown = (event: React.KeyboardEvent<HTMLDivElement>) => {
    const current = SETTINGS_SECTIONS.indexOf(active);
    let next: number | null = null;

    if (event.key === "ArrowRight") next = (current + 1) % SETTINGS_SECTIONS.length;
    else if (event.key === "ArrowLeft")
      next = (current - 1 + SETTINGS_SECTIONS.length) % SETTINGS_SECTIONS.length;
    else if (event.key === "Home") next = 0;
    else if (event.key === "End") next = SETTINGS_SECTIONS.length - 1;

    if (next === null) return;
    event.preventDefault();
    const target = SETTINGS_SECTIONS[next];
    onChange(target);
    document.getElementById(settingsTabId(target))?.focus();
  };

  return (
    <nav className="settings-nav" aria-label="Settings sections">
      <div role="tablist" aria-label="Settings sections" onKeyDown={handleKeyDown}>
        {SETTINGS_SECTIONS.map((section) => {
          const selected = section === active;
          return (
            <button
              key={section}
              type="button"
              role="tab"
              id={settingsTabId(section)}
              aria-selected={selected}
              aria-controls={settingsPanelId(section)}
              // Only the selected tab is in the tab order; arrows move within the set.
              tabIndex={selected ? 0 : -1}
              className={selected ? "settings-tab settings-tab-active" : "settings-tab"}
              onClick={() => onChange(section)}
            >
              {SETTINGS_SECTION_LABELS[section]}
            </button>
          );
        })}
      </div>
    </nav>
  );
});
