import { formatTime, type SessionRecord } from "./api";

export function SessionTechnicalDetails({
  sessionId,
  sessionRecord,
}: {
  sessionId: string | null;
  sessionRecord: SessionRecord | null;
}) {
  return (
    <div className="session-technical-details">
      <h3>Session details</h3>
      <div className="meta">
        <div>
          <p className="meta-label">Session ID</p>
          <p className="meta-value">
            <code>{sessionId || "--"}</code>
          </p>
        </div>
        <div>
          <p className="meta-label">Started</p>
          <p className="meta-value">{formatTime(sessionRecord?.startedAtMs ?? null)}</p>
        </div>
        <div>
          <p className="meta-label">Ended</p>
          <p className="meta-value">{formatTime(sessionRecord?.endedAtMs ?? null)}</p>
        </div>
      </div>
    </div>
  );
}
