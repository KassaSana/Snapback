import { useCallback, useMemo, useState } from "react";

import { api, type AppRuleKind, type AppRuleRecord } from "./api";
import { buildAppRulePreview } from "./appRulePreview";

export const APP_RULE_KINDS: AppRuleKind[] = ["allow", "block"];

export const ruleKindLabel = (kind: AppRuleKind) => (kind === "allow" ? "Allow" : "Block");

export const useAppRules = () => {
  const [appRules, setAppRules] = useState<AppRuleRecord[]>([]);
  const [rulePattern, setRulePattern] = useState("");
  const [ruleKind, setRuleKind] = useState<AppRuleKind>("allow");
  const [ruleNote, setRuleNote] = useState("");
  const [rulesStatus, setRulesStatus] = useState<string | null>(null);

  const refreshAppRules = useCallback(async () => {
    try {
      const rules = await api.getAppRules();
      setAppRules(rules);
    } catch {
      setRulesStatus("Could not load app rules.");
    }
  }, []);

  const handleAddAppRule = useCallback(async () => {
    const pattern = rulePattern.trim();
    if (!pattern) {
      setRulesStatus("Enter an app name or keyword (e.g. discord, notion).");
      return;
    }

    try {
      const saved = await api.upsertAppRule(pattern, ruleKind, ruleNote.trim() || undefined);
      await refreshAppRules();
      setRulePattern("");
      setRuleNote("");
      setRulesStatus(`Saved ${ruleKindLabel(saved.ruleType).toLowerCase()} rule for "${saved.pattern}".`);
    } catch {
      setRulesStatus("Could not save app rule.");
    }
  }, [rulePattern, ruleKind, ruleNote, refreshAppRules]);

  const handleDeleteAppRule = useCallback(async (rule: AppRuleRecord) => {
    try {
      await api.deleteAppRule(rule.id);
      setAppRules((current) => current.filter((entry) => entry.id !== rule.id));
      setRulesStatus(`Removed rule for "${rule.pattern}".`);
    } catch {
      setRulesStatus("Could not delete app rule.");
    }
  }, []);

  const rulePreview = useMemo(
    () => buildAppRulePreview(rulePattern, ruleKind, ruleNote),
    [rulePattern, ruleKind, ruleNote],
  );

  return {
    appRules,
    handleAddAppRule,
    handleDeleteAppRule,
    refreshAppRules,
    ruleKind,
    ruleKindLabel,
    ruleKinds: APP_RULE_KINDS,
    ruleNote,
    rulePattern,
    rulePreview,
    rulesStatus,
    setRuleKind,
    setRuleNote,
    setRulePattern,
  };
};
