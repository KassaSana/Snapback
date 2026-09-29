// Navigation between the three surfaces (ADR-0003): a real tablist, since the panel is swapped
// in place, with arrow-key movement.

import { memo } from "react";

export const SURFACES = ["now", "review", "settings"] as const;

export type Surface = (typeof SURFACES)[number];

const LABELS: Record<Surface, string> = {
  now: "Now",
  review: "Review",
  settings: "Settings",
};

const ICON_PATHS: Record<Surface, string> = {
  now: "M9 5 19 12 9 19Z",
  review: "M5 19V13M12 19V5M19 19V9",
  settings: "M4 7h16M4 17h16M9 4v6M15 14v6",
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
  // Left/Right move between tabs, Home/End jump to the ends — the standard tablist
  // contract. Without this, a keyboard user can reach the tabs but not traverse them.
  const handleKeyDown = (event: React.KeyboardEvent<HTMLDivElement>) => {
    const current = SURFACES.indexOf(active);
    let next: number | null = null;

    if (event.key === "ArrowRight") next = (current + 1) % SURFACES.length;
    else if (event.key === "ArrowLeft") next = (current - 1 + SURFACES.length) % SURFACES.length;
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
      <div role="tablist" aria-label="Sections" onKeyDown={handleKeyDown}>
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
                viewBox="0 0 24 24"
                aria-hidden="true"
                fill="none"
                stroke="currentColor"
                strokeWidth="1.7"
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
