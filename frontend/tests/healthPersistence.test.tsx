import { act, render, renderHook, screen } from "@testing-library/react";
import { beforeEach, expect, it, vi } from "vitest";

const boundary = vi.hoisted(() => ({ invoke: vi.fn(), listen: vi.fn() }));
vi.mock("../src/bridge", () => boundary);
import { useHealth } from "../src/useHealth";
import { api } from "../src/api";
import { ActionErrorBanner } from "../src/ActionErrorBanner";

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

it("a persistence push preserves capture failure precedence", async () => {
  boundary.invoke.mockResolvedValue({ status: "capture_failed", captureFailed: true });
  const { result } = renderHook(() => useHealth());
  await act(async () => {
    await result.current.refreshHealth();
  });
  act(() => result.current.applyPersistenceFailure({ reason: "disk_full", message: "Retrying" }));
  expect(result.current.healthStatus).toBe("offline");
  expect(result.current.persistenceFailureReason).toBe("Retrying");
});

it("the saving warning stays visible until authoritative recovery clears it", () => {
  const { rerender } = render(<ActionErrorBanner error="Saving failed. Retrying automatically." />);
  expect(screen.getByRole("alert")).toHaveTextContent("Retrying automatically");
  expect(screen.queryByRole("button", { name: "Dismiss" })).toBeNull();
  rerender(<ActionErrorBanner error={null} />);
  expect(screen.queryByRole("alert")).toBeNull();
});
