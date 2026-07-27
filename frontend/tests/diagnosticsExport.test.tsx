import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

const boundary = vi.hoisted(() => {
  const invoke = vi.fn(async (command: string): Promise<unknown> => {
    if (command === "get_diagnostics") {
      return {
        version: "9.8.7-test",
        health: {
          status: "online",
          capture_running: true,
          capture_events_dropped: 0,
          classifier: { backend: "heuristic" },
          permissions: {},
        },
        recent_logs: ["startup complete"],
        supportBundlePrivacyNotice:
          "Contains health and logs. It excludes the database. Health details and logs may contain local paths.",
      };
    }
    if (command === "export_support_bundle") {
      return {
        outputPath: "/tmp/snapback-support.json",
        privacyNotice: "Review before sharing.",
      };
    }
    return null;
  });
  return { invoke };
});

vi.mock("../src/bridge", () => ({
  invoke: boundary.invoke,
  listen: vi.fn(async () => () => {}),
}));

import { DiagnosticsCard } from "../src/DiagnosticsCard";

afterEach(() => {
  cleanup();
  boundary.invoke.mockClear();
});

describe("Diagnostics support export", () => {
  it("states the privacy boundary and exports the bundle on demand", async () => {
    render(<DiagnosticsCard />);

    expect(await screen.findByText(/It excludes the database/i)).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Export support bundle" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("export_support_bundle"),
    );
    expect(await screen.findByText(/snapback-support\.json/i)).toBeInTheDocument();
  });
});
