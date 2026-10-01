import { act, renderHook } from "@testing-library/react";
import { beforeEach, expect, it, vi } from "vitest";

const boundary = vi.hoisted(() => ({ invoke: vi.fn(), listen: vi.fn() }));
vi.mock("../src/bridge", () => boundary);
import { useHealth } from "../src/useHealth";
import { api } from "../src/api";

beforeEach(() => vi.resetAllMocks());

it("a failure push prevents an older healthy read from clearing the warning", async () => {
  let resolve!: (value: unknown) => void;
  boundary.invoke.mockImplementation(
    () =>
      new Promise((done) => {
        resolve = done;
      }),
  );
  const { result } = renderHook(() => useHealth());
  let request!: Promise<void>;
  act(() => {
    request = result.current.refreshHealth();
  });
  act(() =>
    result.current.applyPersistenceFailure({
      reason: "disk_full",
      message: "Retrying automatically",
    }),
  );
  await act(async () => {
    resolve({ status: "online" });
    await request;
  });
  expect(result.current.healthStatus).toBe("degraded");
  expect(result.current.persistenceFailureReason).toBe("Retrying automatically");
});

it("authoritative recovery clears the current warning", async () => {
  boundary.invoke.mockResolvedValue({ status: "online", persistenceFailureReason: null });
  const { result } = renderHook(() => useHealth());
  act(() =>
    result.current.applyPersistenceFailure({
      reason: "database_busy",
      message: "Retrying automatically",
    }),
  );
  await act(async () => {
    await result.current.refreshHealth();
  });
  expect(result.current.persistenceFailureReason).toBeNull();
  expect(result.current.healthStatus).toBe("online");
});

it("recovery is a registered native event", async () => {
  boundary.listen.mockResolvedValue(() => {});
  const handler = vi.fn();
  await api.onPersistenceRecovered(handler);
  expect(boundary.listen).toHaveBeenCalledWith("persistence-recovered", handler);
});
