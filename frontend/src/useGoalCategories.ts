import { useCallback, useEffect, useState } from "react";

import { api, type GoalCategory } from "./api";

export const useGoalCategories = () => {
  const [categories, setCategories] = useState<GoalCategory[]>([]);
  const [status, setStatus] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    try {
      setCategories(await api.getGoalCategories());
      setStatus(null);
    } catch {
      setStatus("Could not load goal categories.");
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const update = useCallback((index: number, field: keyof GoalCategory, value: string) => {
    setCategories((current) => current.map((category, position) => {
      if (position !== index) return category;
      return field === "name"
        ? { ...category, name: value }
        : { ...category, keywords: value.split(",").map((keyword) => keyword.trim()).filter(Boolean) };
    }));
  }, []);

  const save = useCallback(async () => {
    try {
      setCategories(await api.setGoalCategories(categories));
      setStatus("Saved goal categories.");
    } catch {
      setStatus("Could not save goal categories.");
    }
  }, [categories]);

  return { categories, refresh, save, status, update };
};
