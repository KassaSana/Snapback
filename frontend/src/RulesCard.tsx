import type { AppRuleKind, AppRuleRecord } from "./api";

type RulesCardProps = {
  appRules: AppRuleRecord[];
  handleAddAppRule: () => void | Promise<void>;
  handleDeleteAppRule: (rule: AppRuleRecord) => void | Promise<void>;
  ruleKind: AppRuleKind;
  ruleKindLabel: (kind: AppRuleKind) => string;
  ruleKinds: AppRuleKind[];
  ruleNote: string;
  rulePattern: string;
  rulePreview: string | null;
  rulesStatus: string | null;
  setRuleKind: (value: AppRuleKind) => void;
  setRuleNote: (value: string) => void;
  setRulePattern: (value: string) => void;
};

export function RulesCard({
  appRules,
  handleAddAppRule,
  handleDeleteAppRule,
  ruleKind,
  ruleKindLabel,
  ruleKinds,
  ruleNote,
  rulePattern,
  rulePreview,
  rulesStatus,
  setRuleKind,
  setRuleNote,
  setRulePattern,
}: RulesCardProps) {
  return (
    <section className="card rules-card">
      <div className="card-header">
        <h2>Personal App Rules</h2>
        <span className="pill">your overrides</span>
      </div>
      <p className="helper-text">
        Match part of an app name or window title. Allow marks matching windows as on-task for
        you. Block raises distraction scoring for matches — it does not close or block apps.
      </p>
      <label className="field">
        <span>Pattern</span>
        <input
          type="text"
          placeholder="discord, notion, youtube"
          value={rulePattern}
          onChange={(event) => setRulePattern(event.target.value)}
        />
      </label>
      <label className="field">
        <span>Rule type</span>
        <select
          value={ruleKind}
          onChange={(event) => setRuleKind(event.target.value as AppRuleKind)}
        >
          {ruleKinds.map((kind) => (
            <option key={kind} value={kind}>
              {ruleKindLabel(kind)}
            </option>
          ))}
        </select>
      </label>
      <label className="field">
        <span>Note (optional)</span>
        <input
          type="text"
          placeholder="study group server"
          value={ruleNote}
          onChange={(event) => setRuleNote(event.target.value)}
        />
      </label>
      {rulePreview ? <p className="rule-preview">{rulePreview}</p> : null}
      <button className="primary-button" onClick={() => void handleAddAppRule()}>
        Save rule
      </button>
      <ul className="rules-list">
        {appRules.length === 0 ? (
          <li className="rules-empty">No personal rules yet.</li>
        ) : (
          appRules.map((rule) => (
            <li key={rule.id} className="rules-item">
              <div>
                <div className="rules-item-header">
                  <span className="rules-pattern">{rule.pattern}</span>
                  <span className={`rules-badge rules-badge-${rule.ruleType}`}>
                    {ruleKindLabel(rule.ruleType)}
                  </span>
                </div>
                {rule.note ? <p className="rules-note">{rule.note}</p> : null}
              </div>
              <button
                className="secondary-button rules-delete"
                onClick={() => void handleDeleteAppRule(rule)}
              >
                Remove
              </button>
            </li>
          ))
        )}
      </ul>
      {rulesStatus ? <p className="helper-text">{rulesStatus}</p> : null}
    </section>
  );
}
