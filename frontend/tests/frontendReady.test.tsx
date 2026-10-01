import { afterEach, describe, expect, it, vi } from "vitest";
import { afterFirstPaint } from "../src/frontendReady";

afterEach(() => vi.unstubAllGlobals());

describe("frontend readiness", () => {
  function frames() {
    let id = 0;
    const queued = new Map<number, () => void>();
    vi.stubGlobal("requestAnimationFrame", (callback: () => void) => {
      queued.set(++id, callback);
      return id;
    });
    vi.stubGlobal("cancelAnimationFrame", (token: number) => queued.delete(token));
    return () => {
      const callbacks = [...queued.values()];
      queued.clear();
      callbacks.forEach((callback) => callback());
    };
  }
  it("acknowledges only after two frame boundaries", () => {
    const advance = frames();
    const ready = vi.fn();
    afterFirstPaint(ready);
    expect(ready).not.toHaveBeenCalled();
    advance();
    expect(ready).not.toHaveBeenCalled();
    advance();
    expect(ready).toHaveBeenCalledOnce();
  });
  it("cancels the second frame when the mounted view is removed", () => {
    const advance = frames();
    const ready = vi.fn();
    const cancel = afterFirstPaint(ready);
    advance();
    cancel();
    advance();
    expect(ready).not.toHaveBeenCalled();
  });
});
