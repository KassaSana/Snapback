import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Mock the Tauri boundary so the real api.ts + useTrainingDeploy run end to end.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    deployStatus: Record<string, unknown>;
    trainResult: Record<string, unknown>;
  } = { health: {}, deployStatus: {}, trainResult: {} };

  const invoke = vi.fn(async (cmd: string): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "get_training_deploy_status":
        return state.deployStatus;
      case "train_from_export":
        return state.trainResult;
      case "reload_classifier_model":
        return { backend: "onnx", onnx_runtime_enabled: true, model_path: "data/model.onnx" };
      case "get_prediction_history":
      case "get_app_rules":
      case "get_context_timeline":
        return [];
      default:
        return null;
    }
  });

  const listen = vi.fn(async () => () => {});
  return { state, invoke, listen };
});

vi.mock("@tauri-apps/api/core", () => ({ invoke: boundary.invoke }));
vi.mock("@tauri-apps/api/event", () => ({ listen: boundary.listen }));

import App from "../src/App";

const healthyCaptureRunning = (): Record<string, unknown> => ({
  status: "online",
  capture_running: true,
  capture_failed: false,
  capture_events_dropped: 0,
  permissions: {
    capture_available: true,
    capture_probe_confirmed: true,
    active_window_available: true,
    message: "",
    setup_steps: [],
  },
  classifier: { backend: "heuristic", onnx_runtime_enabled: false, model_path: null },
});

const readyToTrain = (): Record<string, unknown> => ({
  has_export: true,
  repo_configured: true,
  python_available: true,
  feature_count: 100,
  label_count: 20,
  label_breakdown: { DEEP_FOCUS: 10, DISTRACTED: 10 },
  export_dir: "data",
  pipeline_command: "py -m ml.pipeline_cli",
});

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.deployStatus = readyToTrain();
  boundary.state.trainResult = {};
});

afterEach(() => {
  cleanup();
});

describe("Training / deploy card", () => {
  it("disables 'Train from export' until export + repo + python are ready", async () => {
    boundary.state.deployStatus = { ...readyToTrain(), has_export: false };
    render(<App />);

    const trainButton = await screen.findByRole("button", { name: "Train from export" });
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_training_deploy_status"));
    expect(trainButton).toBeDisabled();
  });

  it("warns and does NOT reload when training succeeds but ONNX isn't deployable", async () => {
    boundary.state.trainResult = {
      success: true,
      training_succeeded: true,
      deploy_ready: false,
      onnx_exported: false,
      message: "ONNX export skipped",
      metrics: null,
      log_tail: "",
    };
    render(<App />);

    const trainButton = await screen.findByRole("button", { name: "Train from export" });
    await waitFor(() => expect(trainButton).not.toBeDisabled());
    fireEvent.click(trainButton);

    expect(await screen.findByText(/Deploy not ready/i)).toBeInTheDocument();
    // Critical: a non-deployable train must not swap the live classifier.
    expect(boundary.invoke).not.toHaveBeenCalledWith("reload_classifier_model");
  });

  it("surfaces a failure message and does not reload when training fails", async () => {
    boundary.state.trainResult = {
      success: false,
      training_succeeded: false,
      deploy_ready: false,
      onnx_exported: false,
      message: "Python 3 not found",
      metrics: null,
      log_tail: "",
    };
    render(<App />);

    const trainButton = await screen.findByRole("button", { name: "Train from export" });
    await waitFor(() => expect(trainButton).not.toBeDisabled());
    fireEvent.click(trainButton);

    expect(await screen.findByText(/Python 3 not found/i)).toBeInTheDocument();
    expect(boundary.invoke).not.toHaveBeenCalledWith("reload_classifier_model");
  });

  it("reloads the classifier when training is deploy-ready", async () => {
    boundary.state.trainResult = {
      success: true,
      training_succeeded: true,
      deploy_ready: true,
      onnx_exported: true,
      message: "Trained and exported",
      metrics: { cv_accuracy: 0.7 },
      log_tail: "",
    };
    render(<App />);

    const trainButton = await screen.findByRole("button", { name: "Train from export" });
    await waitFor(() => expect(trainButton).not.toBeDisabled());
    fireEvent.click(trainButton);

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("reload_classifier_model"),
    );
    expect(await screen.findByText("Loaded trained ONNX model.")).toBeInTheDocument();
  });
});
