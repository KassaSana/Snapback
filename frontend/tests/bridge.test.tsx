import { afterEach, describe, expect, it, vi } from "vitest";

import { invoke, listen, type SnapbackEvent } from "../src/bridge";

afterEach(() => {
  delete window.__snapback;
});

describe("native bridge adapter", () => {
  it("forwards command names and arguments", async () => {
    const nativeInvoke = vi.fn(async () => ({ status: "online" }));
    window.__snapback = {
      invoke: nativeInvoke as unknown as typeof invoke,
      listen: vi.fn(),
    };

    await expect(invoke("get_health", { refresh: true })).resolves.toEqual({ status: "online" });
    expect(nativeInvoke).toHaveBeenCalledWith("get_health", { refresh: true });
  });

  it("registers handlers and returns the native unsubscribe function", async () => {
    const unsubscribe = vi.fn();
    let registered: ((event: SnapbackEvent<{ summary: string }>) => void) | undefined;
    const nativeListen = vi.fn(async (_event, handler) => {
      registered = handler;
      return unsubscribe;
    });
    window.__snapback = {
      invoke: vi.fn(),
      listen: nativeListen,
    };
    const handler = vi.fn();

    const off = await listen("snapback", handler);
    registered?.({ payload: { summary: "return" } });
    off();

    expect(nativeListen).toHaveBeenCalledWith("snapback", handler);
    expect(handler).toHaveBeenCalledWith({ payload: { summary: "return" } });
    expect(unsubscribe).toHaveBeenCalledOnce();
  });

  it("fails clearly when the native page bridge is absent", () => {
    expect(() => invoke("get_health")).toThrow("Snapback bridge is unavailable");
  });
});
