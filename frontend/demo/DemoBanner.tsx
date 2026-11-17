/**
 * The note above the hosted demo saying the data is generated.
 *
 * Styles are inline because styles.css ships inside the desktop app, and this only exists for
 * the demo. Colours follow the app palette (cool neutrals, pine accent).
 */

const wrap: React.CSSProperties = {
  background: "var(--rail)",
  borderBottom: "1px solid var(--border)",
  color: "var(--ink)",
  padding: "10px 20px",
  font: "13px/1.5 var(--font-sans)",
  flex: "none",
  display: "flex",
  flexWrap: "wrap",
  gap: "4px 14px",
  alignItems: "baseline",
};

const label: React.CSSProperties = { fontWeight: 600, color: "var(--accent)" };

const muted: React.CSSProperties = { color: "var(--muted)" };

const link: React.CSSProperties = { color: "var(--ink)", textDecoration: "underline" };

export default function DemoBanner() {
  return (
    <div style={wrap}>
      <span style={label}>Demo</span>
      <span>Everything here is generated sample data, and it resets when you reload.</span>
      <span style={muted}>
        The real app reads the active window and input activity from the OS, which a browser
        can&apos;t do.
      </span>
      <a style={link} href="https://github.com/KassaSana/Snapback">
        Source on GitHub
      </a>
    </div>
  );
}
