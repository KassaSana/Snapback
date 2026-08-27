/**
 * The standing disclosure above the demo.
 *
 * It is not decoration. Snapback's whole claim is that it measures *your* real activity
 * locally, and this page measures nothing — a visitor who assumes otherwise would read invented
 * numbers as evidence. So the banner says what the data is, and why a browser cannot produce
 * the real thing, before they scroll into a dashboard that looks convincing.
 *
 * Styles are inline rather than in `styles.css`: that stylesheet is shipped inside the desktop
 * app, and nothing that exists only for the demo belongs in it.
 */

const wrap: React.CSSProperties = {
  background: "linear-gradient(90deg, #1f2937, #111827)",
  borderBottom: "1px solid rgba(148, 163, 184, 0.28)",
  color: "#e2e8f0",
  padding: "10px 20px",
  font: "500 13px/1.5 system-ui, -apple-system, Segoe UI, sans-serif",
  display: "flex",
  flexWrap: "wrap",
  gap: "6px 14px",
  alignItems: "baseline",
};

const tag: React.CSSProperties = {
  background: "#38bdf8",
  color: "#082f49",
  borderRadius: "999px",
  padding: "2px 9px",
  fontSize: "11px",
  fontWeight: 700,
  letterSpacing: "0.04em",
  textTransform: "uppercase",
};

const muted: React.CSSProperties = { color: "#94a3b8" };

const link: React.CSSProperties = { color: "#7dd3fc", textDecoration: "underline" };

export default function DemoBanner() {
  return (
    <div style={wrap}>
      <span style={tag}>Demo</span>
      <span>Every number on this page is generated sample data.</span>
      <span style={muted}>
        Snapback reads the active window and input idle time from the operating system, which a
        browser tab cannot do — so the real app is a desktop build.
      </span>
      <a style={link} href="https://github.com/KassaSana/Snapback">
        Source on GitHub
      </a>
    </div>
  );
}
