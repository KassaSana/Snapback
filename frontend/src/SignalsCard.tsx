// The rolling signal breakdown (thrash, drift, goal fit, state, risk). On Settings next to
// Diagnostics (ADR-0003): it is for debugging the classifier.

type Props = {
  signals: string[];
};

export function SignalsCard({ signals }: Props) {
  return (
    <section className="card signals-card">
      <div className="card-header">
        <h2>Signals</h2>
        <span className="pill">rolling 30s</span>
      </div>
      <ul className="signal-list">
        {signals.map((signal, index) => (
          <li key={`${signal}-${index}`}>{signal}</li>
        ))}
      </ul>
    </section>
  );
}
