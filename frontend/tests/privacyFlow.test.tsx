import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const boundary = vi.hoisted(() => {
  const state = {
    privacy: { private_mode: false, excluded_apps: [], local_only: true } as Record<string, unknown>,
    dataFolder: { path: "", supported: true, opened: true } as Record<string, unknown>,
    dataFolderThrows: false,
    myDataExport: {} as Record<string, unknown>,
    exportThrows: false,
    health: {
      status: "online",
      capture_running: true,
      capture_failed: false,
      capture_events_dropped: 0,
      permissions: { capture_available: true, capture_probe_confirmed: true, active_window_available: true, message: "", setup_steps: [] },
      classifier: { backend: "heuristic", onnx_runtime_enabled: false, model_path: null },
    },
  };
  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>) => {
    switch (cmd) {
      case "get_privacy_settings": return state.privacy;
      case "set_private_mode":
        state.privacy = { ...state.privacy, private_mode: Boolean(args?.enabled) };
        return state.privacy;
      case "set_privacy_exclusions":
        state.privacy = { ...state.privacy, excluded_apps: args?.excludedApps ?? [] };
        return state.privacy;
      case "delete_all_activity_data": return null;
      case "open_data_folder":
        if (state.dataFolderThrows) throw new Error("no data dir");
        return state.dataFolder;
      case "export_my_data":
        if (state.exportThrows) throw new Error("export failed");
        return state.myDataExport;
      case "get_health": return state.health;
      case "get_settings": return { default_focus_mode: "normal" };
      case "get_active_session": case "get_latest_prediction": return null;
      case "get_prediction_history": case "get_app_rules": case "get_context_timeline": case "get_pomodoro_status": return [];
      case "get_focus_summary": return {};
      case "get_training_deploy_status": return {};
      default: return null;
    }
  });
  const listen = vi.fn(async () => () => {});
  return { invoke, listen, state };
});

vi.mock("../src/bridge", () => ({ invoke: boundary.invoke, listen: boundary.listen }));

import { renderApp } from "./renderApp";

beforeEach(() => {
  boundary.invoke.mockClear();
  boundary.state.privacy = { private_mode: false, excluded_apps: [], local_only: true };
  boundary.state.dataFolder = { path: "", supported: true, opened: true };
  boundary.state.dataFolderThrows = false;
  boundary.state.myDataExport = {
    outputPath: "/data/exports/personal/snapback_my_data.md",
    sessionCount: 3,
    windowCount: 40,
    truncated: false,
  };
  boundary.state.exportThrows = false;
});

afterEach(() => cleanup());

describe("privacy controls", () => {
  it("toggles private mode through the backend", async () => {
    renderApp("settings");
    const toggle = await screen.findByRole("checkbox", { name: "Private mode" });
    await waitFor(() => expect(toggle).not.toBeDisabled());
    fireEvent.click(toggle);
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("set_private_mode", { enabled: true }));
    expect(toggle).toBeChecked();
  });

  it("adds and removes an excluded app", async () => {
    renderApp("settings");
    const input = await screen.findByPlaceholderText("Banking, 1Password");
    fireEvent.change(input, { target: { value: "Banking" } });
    fireEvent.click(screen.getByRole("button", { name: "Add exclusion" }));
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("set_privacy_exclusions", { excludedApps: ["Banking"] }));
    expect(await screen.findByText("Banking")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Remove" }));
    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("set_privacy_exclusions", { excludedApps: [] }));
  });

  it("warns before saving a broad one-character exclusion", async () => {
    renderApp("settings");
    const input = await screen.findByPlaceholderText("Banking, 1Password");
    fireEvent.change(input, { target: { value: "a" } });
    expect(screen.getByText("A one-character exclusion can hide many unrelated apps.")).toBeInTheDocument();
  });

  it("requires confirmation before deleting all local activity data", async () => {
    renderApp("settings");
    fireEvent.click(await screen.findByRole("button", { name: "Delete all activity data" }));

    expect(boundary.invoke).not.toHaveBeenCalledWith("delete_all_activity_data");
    fireEvent.click(screen.getByRole("button", { name: "Confirm permanent deletion" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("delete_all_activity_data"),
    );
    expect(
      await screen.findByText("All locally collected activity data was deleted."),
    ).toBeInTheDocument();
  });
});

// Roadmap 7.6, "open the data folder". Every case here reports the path, because the point of
// the feature is telling the user where their data is — opening the file manager is only the
// convenient version of that answer.
describe("data folder", () => {
  const clickShowDataFolder = async () => {
    renderApp("settings");
    fireEvent.click(await screen.findByRole("button", { name: "Show data folder" }));
  };

  it("opens the folder and names the path it opened", async () => {
    boundary.state.dataFolder = { path: "/Users/kassa/Snapback", supported: true, opened: true };
    await clickShowDataFolder();

    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("open_data_folder"));
    expect(await screen.findByText("Opened /Users/kassa/Snapback")).toBeInTheDocument();
  });

  it("still shows the path when the OS refused to open it", async () => {
    boundary.state.dataFolder = { path: "/Users/kassa/Snapback", supported: true, opened: false };
    await clickShowDataFolder();

    expect(
      await screen.findByText("Could not open the folder. Your data is in /Users/kassa/Snapback"),
    ).toBeInTheDocument();
  });

  // A platform with no file-manager backend must not read as a failure — nothing is broken,
  // this build simply cannot open a window, and the path is the complete answer.
  it("distinguishes an unsupported platform from a failure", async () => {
    boundary.state.dataFolder = { path: "/var/lib/snapback", supported: false, opened: false };
    await clickShowDataFolder();

    expect(
      await screen.findByText("This build cannot open a file manager. Your data is in /var/lib/snapback"),
    ).toBeInTheDocument();
    expect(screen.queryByText(/Could not open the folder/)).toBeNull();
  });

  it("reports a failed command instead of an empty path", async () => {
    boundary.state.dataFolderThrows = true;
    await clickShowDataFolder();

    expect(await screen.findByText("Could not locate the data folder.")).toBeInTheDocument();
  });
});

// Roadmap 7.6, "export my data in a legible form" — distinct from "Export training data",
// which writes a feature matrix for the model rather than a record a person can read.
describe("legible data export", () => {
  const clickExport = async () => {
    renderApp("settings");
    fireEvent.click(await screen.findByRole("button", { name: "Export my data" }));
  };

  it("names the file and what it holds, not just success", async () => {
    await clickExport();

    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("export_my_data"));
    expect(
      await screen.findByText(
        "Wrote 3 sessions and 40 captured windows to /data/exports/personal/snapback_my_data.md.",
      ),
    ).toBeInTheDocument();
  });

  // An export of an empty history is a success, and it has to read as one — otherwise a
  // first-run user cannot tell "nothing was collected" from "the export broke".
  it("treats an empty history as a successful export of nothing", async () => {
    boundary.state.myDataExport = {
      outputPath: "/data/snapback_my_data.md",
      sessionCount: 0,
      windowCount: 0,
      truncated: false,
    };
    await clickExport();

    expect(
      await screen.findByText("Wrote 0 sessions and 0 captured windows to /data/snapback_my_data.md."),
    ).toBeInTheDocument();
  });

  it("passes the truncation warning through instead of implying completeness", async () => {
    boundary.state.myDataExport = {
      outputPath: "/data/snapback_my_data.md",
      sessionCount: 1,
      windowCount: 1,
      truncated: true,
    };
    await clickExport();

    expect(await screen.findByText(/Older sessions were left out\./)).toBeInTheDocument();
    // Singular, so a one-session export does not read as machine output.
    expect(screen.getByText(/Wrote 1 session and 1 captured window /)).toBeInTheDocument();
  });

  it("surfaces a failed export", async () => {
    boundary.state.exportThrows = true;
    await clickExport();

    expect(await screen.findByText("Could not export your data.")).toBeInTheDocument();
  });
});
