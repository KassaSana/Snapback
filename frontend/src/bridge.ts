// The host-to-frontend event envelope. It carries the payload and nothing else.
//
// It used to also carry `event` (the name) and `id` (the shim's internal listener-
// registration counter), which is the envelope shape Tauri hands its listeners. No handler
// ever read either one: `event` is the string the caller already passed to `listen`, and
// `id` was an implementation detail of the shim's callback table leaking to its consumers.
// The wrapper itself is kept -- it is the seam for adding a field later without touching
// every call site -- but it should stay empty of things nothing reads.
export type SnapbackEvent<T> = {
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
