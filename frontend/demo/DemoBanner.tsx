/**
 * The note above the hosted demo saying the data is generated.
 *
 * Styles are inline because styles.css ships inside the desktop app, and this only exists for
 * the demo. Colours follow the app palette (cream background, terracotta accent).
 */

const wrap: React.CSSProperties = {
  background: "#f1ece1",
  borderBottom: "1px solid #e2d9c8",
  color: "#2b2a26",
  padding: "10px 20px",
  font: "13px/1.5 system-ui, -apple-system, Segoe UI, sans-serif",
  display: "flex",
  flexWrap: "wrap",
  gap: "4px 14px",
  alignItems: "baseline",
};

const label: React.CSSProperties = { fontWeight: 600, color: "#c65d44" };

const muted: React.CSSProperties = { color: "#6b665c" };

const link: React.CSSProperties = { color: "#2b2a26", textDecoration: "underline" };

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
