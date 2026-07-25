// The rolling signal breakdown — thrash %, drift %, goal fit, focus state, risk level.
//
// Lives on the Settings surface next to Diagnostics (ADR-0003), not on Now. These lines
// exist to debug the classifier, and while a session is running they compete with the one
// thing that matters. Same content as before, demoted rather than deleted: it is genuinely
// useful when the classifier does something surprising.

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
