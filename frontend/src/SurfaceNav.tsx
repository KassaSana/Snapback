// Navigation between the three surfaces defined in ADR-0003.
//
// Implemented as a real tablist rather than styled buttons: the panel it controls is
// swapped in place, which is what `role="tab"` means, and it gives keyboard users
// arrow-key movement for free-ish. Roadmap 10.3 (accessibility) is still open, but the
// shell is the one place worth getting right up front — every surface inherits it.

export const SURFACES = ["now", "review", "settings"] as const;

export type Surface = (typeof SURFACES)[number];

const LABELS: Record<Surface, string> = {
  now: "Now",
  review: "Review",
  settings: "Settings",
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

export function SurfaceNav({ active, onChange }: Props) {
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
              {LABELS[surface]}
            </button>
          );
        })}
      </div>
    </nav>
  );
}
