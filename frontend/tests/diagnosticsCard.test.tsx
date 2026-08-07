import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const boundary = vi.hoisted(() => {
  const invoke = vi.fn(async (cmd: string): Promise<unknown> => {
    if (cmd === "retry_model_deployment_cleanup") {
      return {
        state: "ok",
        message: null,
        preserved_paths: [],
        retry_cleanup_available: false,
        rollback_available: false,
      };
    }
    if (cmd === "get_diagnostics") {
      return {
        version: "0.2.0",
        health: {
          status: "degraded",
          capture_running: true,
          capture_failed: false,
          capture_events_dropped: 0,
          prediction_suppression_reason: "none",
          permissions: {
            capture_available: true,
            capture_probe_confirmed: true,
            active_window_available: true,
            message: "",
            setup_steps: [],
          },
          classifier: { backend: "heuristic", onnx_runtime_enabled: false, model_path: null },
          model_deployment: {
            state: "degraded",
            message: "could not finish committed model deployment cleanup",
            preserved_paths: ["model.onnx", "model_deploy.transaction.json"],
            retry_cleanup_available: true,
            rollback_available: false,
          },
          developer_tools_enabled: false,
        },
        recent_logs: ["model deployment recovery degraded"],
        supportBundlePrivacyNotice: "",
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

beforeEach(() => {
  boundary.invoke.mockReset();
  boundary.invoke.mockImplementation(async (cmd: string): Promise<unknown> => {
    if (cmd === "retry_model_deployment_cleanup") {
      return {
        state: "ok",
        message: null,
        preserved_paths: [],
        retry_cleanup_available: false,
        rollback_available: false,
      };
    }
    if (cmd === "get_diagnostics") {
      return {
        version: "0.2.0",
        health: {
          status: "degraded",
          capture_running: true,
          capture_failed: false,
          capture_events_dropped: 0,
          prediction_suppression_reason: "none",
          permissions: {
            capture_available: true,
            capture_probe_confirmed: true,
            active_window_available: true,
            message: "",
            setup_steps: [],
          },
          classifier: { backend: "heuristic", onnx_runtime_enabled: false, model_path: null },
          model_deployment: {
            state: "degraded",
            message: "could not finish committed model deployment cleanup",
            preserved_paths: ["model.onnx", "model_deploy.transaction.json"],
            retry_cleanup_available: true,
            rollback_available: false,
          },
          developer_tools_enabled: false,
        },
        recent_logs: ["model deployment recovery degraded"],
        supportBundlePrivacyNotice: "",
      };
    }
    return null;
  });
});

afterEach(() => {
  cleanup();
});

describe("DiagnosticsCard model deployment recovery", () => {
  it("shows preserved paths and retries cleanup when degraded", async () => {
    render(<DiagnosticsCard />);

    expect(
      await screen.findByText(/Model deployment cleanup is degraded/i),
    ).toBeInTheDocument();
    expect(
      screen.getByText(/Preserved: model.onnx, model_deploy.transaction.json/i),
    ).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Retry cleanup" }));
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("retry_model_deployment_cleanup"),
    );
    expect(await screen.findByText(/Model deployment cleanup succeeded/i)).toBeInTheDocument();
  });

  it("keeps a degraded message when retry does not clear the debris", async () => {
    boundary.invoke.mockImplementation(async (cmd: string) => {
      if (cmd === "retry_model_deployment_cleanup") {
        return {
          state: "degraded",
          message: "still locked",
          preserved_paths: ["model.onnx"],
          retry_cleanup_available: true,
          rollback_available: false,
        };
      }
      if (cmd === "get_diagnostics") {
        return {
          version: "0.2.0",
          health: {
            status: "degraded",
            capture_running: true,
            capture_failed: false,
            capture_events_dropped: 0,
            prediction_suppression_reason: "none",
            permissions: {
              capture_available: true,
              capture_probe_confirmed: true,
              active_window_available: true,
              message: "",
              setup_steps: [],
            },
            classifier: { backend: "heuristic", onnx_runtime_enabled: false, model_path: null },
            model_deployment: {
              state: "degraded",
              message: "cleanup blocked",
              preserved_paths: [],
              retry_cleanup_available: true,
              rollback_available: false,
            },
            developer_tools_enabled: false,
          },
          recent_logs: [],
          supportBundlePrivacyNotice: "",
        };
      }
      return null;
    });

    render(<DiagnosticsCard />);
    fireEvent.click(await screen.findByRole("button", { name: "Retry cleanup" }));
    expect(await screen.findByText(/Cleanup still degraded: still locked/i)).toBeInTheDocument();
  });

  it("reports when retry cleanup fails", async () => {
    boundary.invoke.mockImplementation(async (cmd: string) => {
      if (cmd === "retry_model_deployment_cleanup") {
        throw new Error("locked");
      }
      if (cmd === "get_diagnostics") {
        return {
          version: "0.2.0",
          health: {
            status: "degraded",
            capture_running: true,
            capture_failed: false,
            capture_events_dropped: 0,
            prediction_suppression_reason: "none",
            permissions: {
              capture_available: true,
              capture_probe_confirmed: true,
              active_window_available: true,
              message: "",
              setup_steps: [],
            },
            classifier: { backend: "heuristic", onnx_runtime_enabled: false, model_path: null },
            model_deployment: {
              state: "degraded",
              message: "cleanup blocked",
              preserved_paths: ["model.onnx"],
              retry_cleanup_available: true,
              rollback_available: false,
            },
            developer_tools_enabled: false,
          },
          recent_logs: [],
          supportBundlePrivacyNotice: "",
        };
      }
      return null;
    });

    render(<DiagnosticsCard />);
    fireEvent.click(await screen.findByRole("button", { name: "Retry cleanup" }));
    expect(
      await screen.findByText(/Could not retry model deployment cleanup/i),
    ).toBeInTheDocument();
  });
});
