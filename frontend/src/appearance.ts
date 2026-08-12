/** Roadmap 10.10. Appearance is a frontend-only preference: it changes how the webview
 *  renders, not what the native side stores. localStorage keeps the choice across restarts
 *  without a new IPC command or settings.json field. */

export type AppearanceMode = "system" | "light" | "dark";

export const APPEARANCE_STORAGE_KEY = "snapback.appearance";
export const APPEARANCE_ATTRIBUTE = "data-appearance";

const VALID: AppearanceMode[] = ["system", "light", "dark"];

export function isAppearanceMode(value: string): value is AppearanceMode {
  return (VALID as string[]).includes(value);
}

export function readAppearanceMode(): AppearanceMode {
  if (typeof globalThis.localStorage === "undefined") return "system";
  const stored = globalThis.localStorage.getItem(APPEARANCE_STORAGE_KEY);
  return stored && isAppearanceMode(stored) ? stored : "system";
}

export function writeAppearanceMode(mode: AppearanceMode): void {
  if (typeof globalThis.localStorage === "undefined") return;
  globalThis.localStorage.setItem(APPEARANCE_STORAGE_KEY, mode);
}

/** Which color-scheme the document should use right now. */
export function resolvedColorScheme(mode: AppearanceMode): "light" | "dark" {
  if (mode === "dark") return "dark";
  if (mode === "light") return "light";
  if (typeof globalThis.matchMedia !== "function") return "light";
  return globalThis.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
}

export function applyAppearance(mode: AppearanceMode, root: HTMLElement = document.documentElement): void {
  root.setAttribute(APPEARANCE_ATTRIBUTE, mode);
  root.style.colorScheme = resolvedColorScheme(mode);
}

export function watchSystemAppearance(onChange: () => void): () => void {
  if (typeof globalThis.matchMedia !== "function") return () => {};
  const query = globalThis.matchMedia("(prefers-color-scheme: dark)");
  const handler = () => onChange();
  query.addEventListener("change", handler);
  return () => query.removeEventListener("change", handler);
}
