export type SnapbackEvent<T> = {
  event: string;
  id: number;
  payload: T;
};

type SnapbackBridge = {
  invoke<T>(command: string, args?: Record<string, unknown>): Promise<T>;
  listen<T>(event: string, handler: (event: SnapbackEvent<T>) => void): Promise<() => void>;
};

declare global {
  interface Window {
    __snapback?: SnapbackBridge;
  }
}

function bridge(): SnapbackBridge {
  if (!window.__snapback) {
    throw new Error("Snapback bridge is unavailable");
  }
  return window.__snapback;
}

export function invoke<T>(command: string, args?: Record<string, unknown>): Promise<T> {
  return bridge().invoke<T>(command, args);
}

export function listen<T>(
  event: string,
  handler: (event: SnapbackEvent<T>) => void,
): Promise<() => void> {
  return bridge().listen<T>(event, handler);
}
