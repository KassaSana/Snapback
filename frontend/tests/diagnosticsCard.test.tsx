import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const boundary = vi.hoisted(() => {
  const invoke = vi.fn(async (cmd: string): Promise<unknown> => {
    if (cmd === "retry_model_deployment_cleanup") {
      return {
        state: "ok",
        message: null,
        preservedPaths: [],
        retryCleanupAvailable: false,
        rollbackAvailable: false,
      };
    }
    if (cmd === "get_diagnostics") {
      return {
        version: "0.2.0",
        health: {
          status: "degraded",
          captureRunning: true,
          captureFailed: false,
          captureEventsDropped: 0,
          predictionSuppressionReason: "none",
          permissions: {
            captureAvailable: true,
            captureProbeConfirmed: true,
            activeWindowAvailable: true,
            message: "",
            setupSteps: [],
          },
          classifier: { backend: "heuristic", onnxRuntimeEnabled: false, modelPath: null },
          modelDeployment: {
            state: "degraded",
            message: "could not finish committed model deployment cleanup",
            preservedPaths: ["model.onnx", "model_deploy.transaction.json"],
            retryCleanupAvailable: true,
            rollbackAvailable: false,
          },
          developerToolsEnabled: false,
        },
        recentLogs: ["model deployment recovery degraded"],
        supportBundlePrivacyNotice: "",
      };
    }
    if (cmd === "open_data_folder") {
      return { path: "C:/Users/Kassa/AppData/Roaming/Snapback", supported: true, opened: true };
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
        preservedPaths: [],
        retryCleanupAvailable: false,
        rollbackAvailable: false,
      };
    }
    if (cmd === "get_diagnostics") {
      return {
        version: "0.2.0",
        health: {
          status: "degraded",
          captureRunning: true,
          captureFailed: false,
          captureEventsDropped: 0,
          predictionSuppressionReason: "none",
          permissions: {
            captureAvailable: true,
            captureProbeConfirmed: true,
            activeWindowAvailable: true,
            message: "",
            setupSteps: [],
          },
          classifier: { backend: "heuristic", onnxRuntimeEnabled: false, modelPath: null },
          modelDeployment: {
            state: "degraded",
            message: "could not finish committed model deployment cleanup",
            preservedPaths: ["model.onnx", "model_deploy.transaction.json"],
            retryCleanupAvailable: true,
            rollbackAvailable: false,
          },
          developerToolsEnabled: false,
        },
        recentLogs: ["model deployment recovery degraded"],
        supportBundlePrivacyNotice: "",
      };
    }
    if (cmd === "open_data_folder") {
      return { path: "C:/Users/Kassa/AppData/Roaming/Snapback", supported: true, opened: true };
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

    fireEvent.click(screen.getByRole("button", { name: "Reveal preserved files" }));
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("open_data_folder"));
    expect(await screen.findByText(/Opened C:\/Users\/Kassa/i)).toBeInTheDocument();

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
          preservedPaths: ["model.onnx"],
          retryCleanupAvailable: true,
          rollbackAvailable: false,
        };
      }
      if (cmd === "get_diagnostics") {
        return {
          version: "0.2.0",
          health: {
            status: "degraded",
            captureRunning: true,
            captureFailed: false,
            captureEventsDropped: 0,
            predictionSuppressionReason: "none",
            permissions: {
              captureAvailable: true,
              captureProbeConfirmed: true,
              activeWindowAvailable: true,
              message: "",
              setupSteps: [],
            },
            classifier: { backend: "heuristic", onnxRuntimeEnabled: false, modelPath: null },
            modelDeployment: {
              state: "degraded",
              message: "cleanup blocked",
              preservedPaths: [],
              retryCleanupAvailable: true,
              rollbackAvailable: false,
            },
            developerToolsEnabled: false,
          },
          recentLogs: [],
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
            captureRunning: true,
            captureFailed: false,
            captureEventsDropped: 0,
            predictionSuppressionReason: "none",
            permissions: {
              captureAvailable: true,
              captureProbeConfirmed: true,
              activeWindowAvailable: true,
              message: "",
              setupSteps: [],
            },
            classifier: { backend: "heuristic", onnxRuntimeEnabled: false, modelPath: null },
            modelDeployment: {
              state: "degraded",
              message: "cleanup blocked",
              preservedPaths: ["model.onnx"],
              retryCleanupAvailable: true,
              rollbackAvailable: false,
            },
            developerToolsEnabled: false,
          },
          recentLogs: [],
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
