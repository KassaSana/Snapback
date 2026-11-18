// Navigation between the three surfaces (ADR-0003): a real tablist, since the panel is swapped
// in place, with arrow-key movement. It sits vertically in the left rail, so Up/Down move too.

import { memo } from "react";

export const SURFACES = ["now", "review", "settings"] as const;

export type Surface = (typeof SURFACES)[number];

const LABELS: Record<Surface, string> = {
  now: "Now",
  review: "Review",
  settings: "Settings",
};

// 16px line icons: a target for the live session, rows for history, sliders for settings.
const ICON_PATHS: Record<Surface, string> = {
  now: "M8 2.5a5.5 5.5 0 1 1 0 11 5.5 5.5 0 0 1 0-11ZM8 6a2 2 0 1 1 0 4 2 2 0 0 1 0-4Z",
  review: "M3 4h10M3 8h7M3 12h9",
  settings: "M3 5h6M12 5h1M3 11h1M7 11h6M10.5 3.5a1.5 1.5 0 1 1 0 3 1.5 1.5 0 0 1 0-3ZM5.5 9.5a1.5 1.5 0 1 1 0 3 1.5 1.5 0 0 1 0-3Z",
};

export function surfaceTabId(surface: Surface): string {
  return `surface-tab-${surface}`;
}

export function surfacePanelId(surface: Surface): string {
  return `surface-panel-${surface}`;
}

type Props = {
  active: Surface;
  onChange: (surface: Surface) => void;
};

export const SurfaceNav = memo(function SurfaceNav({ active, onChange }: Props) {
  // Arrow keys move between tabs, Home/End jump to the ends — the standard tablist
  // contract. Without this, a keyboard user can reach the tabs but not traverse them.
  const handleKeyDown = (event: React.KeyboardEvent<HTMLDivElement>) => {
    const current = SURFACES.indexOf(active);
    let next: number | null = null;

    if (event.key === "ArrowRight" || event.key === "ArrowDown")
      next = (current + 1) % SURFACES.length;
    else if (event.key === "ArrowLeft" || event.key === "ArrowUp")
      next = (current - 1 + SURFACES.length) % SURFACES.length;
    else if (event.key === "Home") next = 0;
    else if (event.key === "End") next = SURFACES.length - 1;

    if (next === null) return;
    event.preventDefault();
    const target = SURFACES[next];
    onChange(target);
    document.getElementById(surfaceTabId(target))?.focus();
  };

  return (
    <nav className="surface-nav" aria-label="Sections">
      <div
        role="tablist"
        aria-label="Sections"
        aria-orientation="vertical"
        onKeyDown={handleKeyDown}
      >
        {SURFACES.map((surface) => {
          const selected = surface === active;
          return (
            <button
              key={surface}
              type="button"
              role="tab"
              id={surfaceTabId(surface)}
              aria-selected={selected}
              aria-controls={surfacePanelId(surface)}
              // Only the selected tab is in the tab order; arrows move within the set.
              tabIndex={selected ? 0 : -1}
              className={selected ? "surface-tab surface-tab-active" : "surface-tab"}
              onClick={() => onChange(surface)}
            >
              <svg
                className="surface-icon"
                viewBox="0 0 16 16"
                aria-hidden="true"
                fill="none"
                stroke="currentColor"
                strokeWidth="1.4"
                strokeLinecap="round"
                strokeLinejoin="round"
              >
                <path d={ICON_PATHS[surface]} />
              </svg>
              {LABELS[surface]}
            </button>
          );
        })}
      </div>
    </nav>
  );
});
