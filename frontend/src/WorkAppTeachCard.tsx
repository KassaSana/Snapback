import { memo, useMemo, useState } from "react";

import type { AppRuleKind, AppRuleRecord, ContextSnapshot } from "./api";
import { shouldShowWorkAppTeach, workAppCandidates, type WorkAppCandidate } from "./workAppTeach";

type Props = {
  appRules: AppRuleRecord[];
  contextTimeline: ContextSnapshot[];
  dismissed: boolean;
  onCreateAppRule: (appName: string, kind: AppRuleKind) => void | Promise<void>;
  onDismiss: () => void;
};

export const WorkAppTeachCard = memo(function WorkAppTeachCard({
  appRules,
  contextTimeline,
  dismissed,
  onCreateAppRule,
  onDismiss,
}: Props) {
  // Apps the user skipped this visit without writing a rule. Not persisted: a later
  // session that sees the same window should be allowed to ask again, unless they
  // dismissed the whole card.
  const [skipped, setSkipped] = useState<string[]>([]);

  const candidates = useMemo(() => {
    const skip = new Set(skipped);
    return workAppCandidates(contextTimeline, appRules).filter(
      (candidate) => !skip.has(candidate.appName),
    );
  }, [appRules, contextTimeline, skipped]);

  if (!shouldShowWorkAppTeach({ dismissed, candidates })) return null;

  return (
    <section className="card work-app-teach-card" aria-labelledby="work-app-teach-heading">
      <div className="card-header">
        <h2 id="work-app-teach-heading">Which of these were the work?</h2>
        <span className="pill">this session</span>
      </div>
      <p className="helper-text">
        Snapback cannot see the screen. Naming the apps that count is how a quiet window stops
        looking like Deep work.
      </p>
      <ul className="work-app-teach-list">
        {candidates.map((candidate) => (
          <WorkAppTeachRow
            key={candidate.appName}
            candidate={candidate}
            onAllow={() => void onCreateAppRule(candidate.appName, "allow")}
            onBlock={() => void onCreateAppRule(candidate.appName, "block")}
            onSkip={() => setSkipped((current) => [...current, candidate.appName])}
          />
        ))}
      </ul>
      <button type="button" className="link-button" onClick={onDismiss}>
        I&apos;ll do this later
      </button>
    </section>
  );
});

function WorkAppTeachRow({
  candidate,
  onAllow,
  onBlock,
  onSkip,
}: {
  candidate: WorkAppCandidate;
  onAllow: () => void;
  onBlock: () => void;
  onSkip: () => void;
}) {
  return (
    <li className="work-app-teach-row">
      <div className="work-app-teach-detail">
        <p className="work-app-teach-name">{candidate.appName}</p>
        {candidate.exampleTitle ? <p className="helper-text">{candidate.exampleTitle}</p> : null}
      </div>
      <div className="timeline-quick-rules">
        <button type="button" className="mini-action-button allow-btn" onClick={onAllow}>
          The work
        </button>
        <button type="button" className="mini-action-button block-btn" onClick={onBlock}>
          Not the work
        </button>
        <button type="button" className="link-button" onClick={onSkip}>
          Skip
        </button>
      </div>
    </li>
  );
}
