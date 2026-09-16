import { act, cleanup, fireEvent, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Mock the native boundary so the real api.ts + useTrainingDeploy run end to end.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    deployStatus: Record<string, unknown>;
    trainResult: Record<string, unknown>;
    // When set, train_from_export returns this instead, so a test can hold the run open.
    trainPending: Promise<Record<string, unknown>> | null;
    exportResult: unknown;
  } = { health: {}, deployStatus: {}, trainResult: {}, trainPending: null, exportResult: {} };

  const invoke = vi.fn(async (cmd: string): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "get_training_deploy_status":
        return state.deployStatus;
      case "train_from_export":
        return state.trainPending ?? state.trainResult;
      case "cancel_training":
        return { requested: true };
      case "export_training_data":
        return state.exportResult;
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

  // Handlers are recorded so a test can push a native event (training-progress) mid-run.
  const listeners: Record<string, Array<(event: { payload: unknown }) => void>> = {};
  const listen = vi.fn(
    async (event: string, handler: (event: { payload: unknown }) => void) => {
      (listeners[event] ??= []).push(handler);
      return () => {
        listeners[event] = (listeners[event] ?? []).filter((h) => h !== handler);
      };
    },
  );
  const emit = (event: string, payload: unknown) => {
    for (const handler of listeners[event] ?? []) handler({ payload });
  };
  const listenerCount = (event: string) => (listeners[event] ?? []).length;
  return { state, invoke, listen, emit, listenerCount };
});

vi.mock("../src/bridge", () => ({ invoke: boundary.invoke, listen: boundary.listen }));

import { renderApp } from "./renderApp";

const healthyCaptureRunning = (): Record<string, unknown> => ({
  status: "online",
  capture_running: true,
  capture_failed: false,
  capture_events_dropped: 0,
  developer_tools_enabled: true,
  permissions: {
    capture_available: true,
    capture_probe_confirmed: true,
    active_window_available: true,
    message: "",
    setup_steps: [],
  },
  classifier: { backend: "heuristic", onnx_runtime_enabled: false, model_path: null },
  model_deployment: { state: "ok", preserved_paths: [], retry_cleanup_available: false },
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
  boundary.state.trainPending = null;
  boundary.state.exportResult = {
    output_dir: "data",
    features_path: "data/features.csv",
    labels_path: "data/labels.csv",
    feature_count: 100,
    label_count: 20,
  };
});

afterEach(() => {
  cleanup();
});

describe("Training / deploy card", () => {
  // Roadmap 10.9 moved the feedback controls into **Focus** and left model tooling in
  // **Advanced**, so this assertion follows the card rather than the surface.
  it("describes the available feedback controls without claiming global hotkeys", async () => {
    renderApp("settings", "focus");

    expect(
      await screen.findByText(/Use these controls to label it while a session is active/i),
    ).toBeInTheDocument();
    expect(screen.queryByText(/Global hotkeys/i)).not.toBeInTheDocument();
  });

  it("hides model tooling when developer tools are off", async () => {
    boundary.state.health = { ...healthyCaptureRunning(), developer_tools_enabled: false };
    renderApp("settings", "advanced");

    // Advanced still renders — it is the diagnostics home too — but the training disclosure
    // is absent entirely rather than present and empty.
    expect(await screen.findByText(/Logs and diagnostics/i)).toBeInTheDocument();
    expect(screen.queryByText(/Model training/i)).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "Train from export" })).not.toBeInTheDocument();
    expect(screen.queryByText(/Model tooling/i)).not.toBeInTheDocument();
    expect(boundary.invoke).not.toHaveBeenCalledWith("get_training_deploy_status");
  });

  it("disables 'Train from export' until export + repo + python are ready", async () => {
    boundary.state.deployStatus = { ...readyToTrain(), has_export: false };
    renderApp("settings", "advanced");

    const trainButton = await screen.findByRole("button", { name: "Train from export" });
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("get_training_deploy_status"));
    expect(trainButton).toBeDisabled();
  });

  it("shows export progress and disables export and training until it resolves", async () => {
    let finishExport!: (value: unknown) => void;
    boundary.state.exportResult = new Promise((resolve) => {
      finishExport = resolve;
    });
    renderApp("settings", "advanced");

    const exportButton = await screen.findByRole("button", { name: "Export training data" });
    const trainButton = await screen.findByRole("button", { name: "Train from export" });
    await waitFor(() => expect(trainButton).not.toBeDisabled());
    fireEvent.click(exportButton);

    const exportingButton = await screen.findByRole("button", { name: "Exporting…" });
    expect(exportingButton).toBeDisabled();
    expect(trainButton).toBeDisabled();

    finishExport({
      output_dir: "data",
      features_path: "data/features.csv",
      labels_path: "data/labels.csv",
      feature_count: 100,
      label_count: 20,
    });
    await waitFor(() =>
      expect(screen.getByRole("button", { name: "Export training data" })).not.toBeDisabled(),
    );
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
    renderApp("settings", "advanced");

    const trainButton = await screen.findByRole("button", { name: "Train from export" });
    await waitFor(() => expect(trainButton).not.toBeDisabled());
    fireEvent.click(trainButton);

    expect(await screen.findByText(/Deploy not ready/i)).toBeInTheDocument();
    // Critical: a non-deployable train must not swap the live classifier.
    expect(boundary.invoke).not.toHaveBeenCalledWith("reload_classifier_model");
  });

  it("does not offer reload for an export rejected by the quality gate", async () => {
    boundary.state.deployStatus = {
      ...readyToTrain(),
      model_onnx_exists: true,
      quality_gate: {
        passed: false,
        reason: "Model rejected: held_out_accuracy=0.59 is below the threshold.",
      },
    };
    renderApp("settings", "advanced");

    const reloadButton = await screen.findByRole("button", { name: "Reload model" });
    expect(reloadButton).toBeDisabled();
    await waitFor(() => {
      expect(screen.getByText("Candidate did not pass the quality gate")).toBeInTheDocument();
    });
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
    renderApp("settings", "advanced");

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
    renderApp("settings", "advanced");

    const trainButton = await screen.findByRole("button", { name: "Train from export" });
    await waitFor(() => expect(trainButton).not.toBeDisabled());
    fireEvent.click(trainButton);

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("reload_classifier_model"),
    );
    expect(await screen.findByText("Loaded trained ONNX model.")).toBeInTheDocument();
  });

  it("offers Cancel while training runs and reports the cancelled run without reloading", async () => {
    // The run stays open until the test releases it, the way a real Python run would; the
    // native side answers the cancel through the run's own result, not the cancel call.
    let finishRun: (result: Record<string, unknown>) => void = () => {};
    boundary.state.trainPending = new Promise((resolve) => {
      finishRun = resolve;
    });
    renderApp("settings", "advanced");

    const trainButton = await screen.findByRole("button", { name: "Train from export" });
    await waitFor(() => expect(trainButton).not.toBeDisabled());
    expect(screen.queryByRole("button", { name: /Cancel training/ })).not.toBeInTheDocument();
    fireEvent.click(trainButton);

    const cancelButton = await screen.findByRole("button", { name: "Cancel training" });
    fireEvent.click(cancelButton);
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("cancel_training"));
    // Acknowledged at once, and not clickable twice.
    const cancelling = await screen.findByRole("button", { name: /Cancelling/ });
    expect(cancelling).toBeDisabled();

    finishRun({
      success: false,
      training_succeeded: false,
      cancelled: true,
      deploy_ready: false,
      onnx_exported: false,
      message: "Training was cancelled before it finished. Nothing was deployed.",
      metrics: null,
      log_tail: "",
    });

    expect(await screen.findByText(/Training was cancelled/)).toBeInTheDocument();
    await waitFor(() =>
      expect(screen.queryByRole("button", { name: /Cancel/ })).not.toBeInTheDocument(),
    );
    expect(screen.getByRole("button", { name: "Train from export" })).not.toBeDisabled();
    expect(boundary.invoke).not.toHaveBeenCalledWith("reload_classifier_model");
  });

  it("shows the native log tail as progress while training runs, and only then", async () => {
    let finishRun: (result: Record<string, unknown>) => void = () => {};
    boundary.state.trainPending = new Promise((resolve) => {
      finishRun = resolve;
    });
    renderApp("settings", "advanced");

    const trainButton = await screen.findByRole("button", { name: "Train from export" });
    await waitFor(() => expect(trainButton).not.toBeDisabled());
    // Nothing listens before a run: the event has no meaning outside one.
    expect(boundary.listenerCount("training-progress")).toBe(0);
    fireEvent.click(trainButton);
    await waitFor(() => expect(boundary.listenerCount("training-progress")).toBe(1));

    act(() => {
      boundary.emit("training-progress", { elapsedMs: 83000, logTail: "epoch 3/10" });
    });
    const status = await screen.findByRole("status");
    expect(status).toHaveTextContent("1m 23s");
    expect(status).toHaveTextContent("epoch 3/10");

    finishRun({
      success: false,
      training_succeeded: false,
      deploy_ready: false,
      onnx_exported: false,
      message: "Training failed. Check the training log for details.",
      metrics: null,
      log_tail: "epoch 3/10",
    });

    expect(await screen.findByText(/Training failed/)).toBeInTheDocument();
    // The progress line goes with the run, and so does the subscription.
    expect(screen.queryByRole("status")).not.toBeInTheDocument();
    await waitFor(() => expect(boundary.listenerCount("training-progress")).toBe(0));
  });
});
