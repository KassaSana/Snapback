import { useCallback, useEffect, useState } from "react";

import {
  applyAppearance,
  readAppearanceMode,
  watchSystemAppearance,
  writeAppearanceMode,
  type AppearanceMode,
} from "./appearance";

export const useAppearance = () => {
  const [mode, setModeState] = useState<AppearanceMode>(() => readAppearanceMode());

  useEffect(() => {
    applyAppearance(mode);
  }, [mode]);

  useEffect(() => {
    if (mode !== "system") return;
    return watchSystemAppearance(() => applyAppearance("system"));
  }, [mode]);

  const setMode = useCallback((next: AppearanceMode) => {
    writeAppearanceMode(next);
    setModeState(next);
  }, []);

  return { mode, setMode };
};
