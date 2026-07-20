import type { AnalyticsSummary } from "./api";

type AnalyticsCardProps = { analytics: AnalyticsSummary };

export function AnalyticsCard({ analytics }: AnalyticsCardProps) {
  const maxFocus = Math.max(1, ...analytics.hourly.map((entry) => entry.avgFocusScore));
  const hourMap = new Map(analytics.hourly.map((entry) => [entry.hour, entry]));

  return (
    <section className="card insights-card analytics-card">
      <div className="card-header">
        <h2>Trends</h2>
        <span className="pill">all recorded sessions</span>
      </div>
      {analytics.sampleCount === 0 ? (
        <p className="helper-text">No prediction data yet. Start a session to build trends.</p>
      ) : (
        <>
          <div className="insight-tiles">
            <div className="insight-tile"><p className="insight-tile-value">{Math.round(analytics.avgFocusScore)}</p><p className="insight-tile-label">Avg focus</p></div>
            <div className="insight-tile"><p className="insight-tile-value">{analytics.sampleCount}</p><p className="insight-tile-label">Samples</p></div>
            <div className="insight-tile"><p className="insight-tile-value">{analytics.productiveSessionStreak}</p><p className="insight-tile-label">Focus streak</p></div>
          </div>
          <svg className="insights-chart" viewBox="0 0 480 150" role="img" aria-label="Average focus by hour">
            <line x1="24" y1="126" x2="468" y2="126" className="chart-baseline" />
            {Array.from({ length: 24 }, (_, hour) => {
              const entry = hourMap.get(hour);
              const height = entry ? (entry.avgFocusScore / maxFocus) * 100 : 0;
              const x = 26 + hour * 18;
              return <rect key={hour} x={x} y={126 - height} width="12" height={height} className="chart-bar"><title>{`${hour}:00 · focus ${Math.round(entry?.avgFocusScore ?? 0)}`}</title></rect>;
            })}
          </svg>
          <p className="insights-caption">Average focus by hour of day</p>
          <ul className="history-list">
            {analytics.topApps.length === 0 ? <li className="history-empty">No app context data yet.</li> : analytics.topApps.map((app) => (
              <li key={app.appName} className="history-item"><span>{app.appName}</span><strong>{app.windowCount} switches</strong></li>
            ))}
          </ul>
        </>
      )}
    </section>
  );
}
