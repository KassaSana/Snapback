import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Roadmap 9.14. Drives the real card + useDataImport + api.ts against the mocked native
// boundary, so a control wired to the wrong command fails here rather than in the app.
const boundary = vi.hoisted(() => {
  const state: {
    health: Record<string, unknown>;
    settings: Record<string, unknown>;
    inspect: Record<string, unknown>;
    stage: Record<string, unknown>;
    pending: boolean;
  } = {
    health: {},
    settings: {},
    inspect: {},
    stage: {},
    pending: false,
  };

  const invoke = vi.fn(async (cmd: string): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "refresh_permissions":
        return (state.health.permissions as Record<string, unknown>) ?? {};
      case "get_settings":
        return state.settings;
      case "get_privacy_settings":
        return { private_mode: false, excluded_apps: [] };
      case "inspect_data_import":
        return state.inspect;
      case "stage_data_import":
        if (state.stage.ok) state.pending = true;
        return state.stage;
      case "cancel_data_import":
        state.pending = false;
        return { cancelled: true, pending: false };
      case "get_data_import_status":
        return { pending: state.pending };
      case "get_prediction_history":
      case "get_app_rules":
      case "get_context_timeline":
      case "get_session_history":
        return [];
      case "get_training_deploy_status":
        return {};
      default:
        return null;
    }
  });

  const listen = vi.fn(async () => () => {});
  return { state, invoke, listen };
});

vi.mock("../src/bridge", () => ({ invoke: boundary.invoke, listen: boundary.listen }));

import App from "../src/App";
import { renderApp } from "./renderApp";

const healthyCaptureRunning = (): Record<string, unknown> => ({
  status: "online",
  capture_running: true,
  capture_failed: false,
  capture_events_dropped: 0,
  developer_tools_enabled: false,
  permissions: {
    capture_available: true,
    capture_probe_confirmed: true,
    active_window_available: true,
    message: "",
    setup_steps: [],
  },
  classifier: { backend: "heuristic", onnx_runtime_enabled: false, model_path: null },
});

beforeEach(() => {
  window.localStorage.clear();
  window.location.hash = "";
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.settings = { default_focus_mode: "normal" };
  boundary.state.inspect = { acceptable: true, message: "", schemaVersion: 6, sessionCount: 42 };
  boundary.state.stage = {
    ok: true,
    message: "Ready to import 42 sessions. Restart Snapback to replace your current data.",
    schemaVersion: 6,
    sessionCount: 42,
  };
  boundary.state.pending = false;
});

afterEach(() => cleanup());

const importCard = async () =>
  (await screen.findByRole("heading", { name: "Import data" })).closest(
    "section",
  ) as HTMLElement;

const typePath = (card: HTMLElement, value: string) =>
  fireEvent.change(within(card).getByLabelText("Database file"), { target: { value } });

const staged = () =>
  boundary.invoke.mock.calls.filter(([cmd]) => cmd === "stage_data_import").length;

describe("data import", () => {
  it("lives with the other data-ownership controls", async () => {
    renderApp("settings", "privacy");
    expect(await screen.findByRole("heading", { name: "Import data" })).toBeInTheDocument();
  });

  // THE RULE this item turns on: replacing is not merging, and the user must be told before
  // they commit, not after the restart when it cannot be undone.
  it("states that importing replaces rather than merges, before staging anything", async () => {
    renderApp("settings", "privacy");
    const card = await importCard();

    typePath(card, "C:/backup/focoflow.db");
    fireEvent.click(within(card).getByRole("button", { name: "Check this file" }));

    // Checking is read-only — nothing is staged by looking.
    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("inspect_data_import", {
        path: "C:/backup/focoflow.db",
      }),
    );
    expect(staged()).toBe(0);

    // Both halves of the trade are stated: what is adopted, and what is lost.
    const notice = await within(card).findByRole("status");
    expect(notice).toHaveTextContent("42");
    expect(notice).toHaveTextContent("replaces");
    expect(notice).toHaveTextContent("not merged");
    expect(notice).toHaveTextContent("backup");
  });

  it("backs out without staging when the user keeps their data", async () => {
    renderApp("settings", "privacy");
    const card = await importCard();

    typePath(card, "C:/backup/focoflow.db");
    fireEvent.click(within(card).getByRole("button", { name: "Check this file" }));
    await within(card).findByRole("button", { name: "Replace my data with this" });

    fireEvent.click(within(card).getByRole("button", { name: "Keep my current data" }));

    expect(
      within(card).queryByRole("button", { name: "Replace my data with this" }),
    ).toBeNull();
    expect(staged()).toBe(0);
  });

  it("shows a refusal verbatim and does not offer to replace anything", async () => {
    boundary.state.inspect = {
      acceptable: false,
      message:
        "That database was written by a newer version of Snapback (format v9; this build understands v6).",
      schemaVersion: 9,
      sessionCount: 0,
    };

    renderApp("settings", "privacy");
    const card = await importCard();

    typePath(card, "C:/backup/newer.db");
    fireEvent.click(within(card).getByRole("button", { name: "Check this file" }));

    // The native side already phrased it for the user; replacing that with something vaguer
    // would strip the one detail that says what to do next.
    expect(await within(card).findByText(/newer version of Snapback/)).toBeInTheDocument();
    expect(
      within(card).queryByRole("button", { name: "Replace my data with this" }),
    ).toBeNull();
    expect(staged()).toBe(0);
  });

  it("stages the import and says it takes effect on restart", async () => {
    renderApp("settings", "privacy");
    const card = await importCard();

    typePath(card, "C:/backup/focoflow.db");
    fireEvent.click(within(card).getByRole("button", { name: "Check this file" }));
    fireEvent.click(await within(card).findByRole("button", { name: "Replace my data with this" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("stage_data_import", {
        path: "C:/backup/focoflow.db",
      }),
    );
    // Nothing has been replaced yet, and the card says exactly that.
    expect(await within(card).findByText(/Restart Snapback/)).toBeInTheDocument();
  });

  // A staged import is the one state where doing nothing is destructive, so the undo has to be
  // reachable right up until the restart.
  it("offers to cancel a staged import, including one staged in a previous run", async () => {
    boundary.state.pending = true;

    renderApp("settings", "privacy");
    const card = await importCard();

    expect(await within(card).findByText(/An import is waiting/)).toBeInTheDocument();
    // With one pending there is no way to stage a second over it.
    expect(within(card).queryByLabelText("Database file")).toBeNull();

    fireEvent.click(within(card).getByRole("button", { name: "Cancel the import" }));

    await waitFor(() => expect(boundary.invoke).toHaveBeenCalledWith("cancel_data_import"));
    expect(await within(card).findByText(/current data will be kept/)).toBeInTheDocument();
    // The form comes back, so the user can try a different file.
    expect(within(card).getByLabelText("Database file")).toBeInTheDocument();
  });

  it("reports a staging failure without claiming anything changed", async () => {
    boundary.state.stage = {
      ok: false,
      message: "Could not read that database all the way through, so nothing was changed.",
      schemaVersion: 0,
      sessionCount: 0,
    };

    renderApp("settings", "privacy");
    const card = await importCard();

    typePath(card, "C:/backup/damaged.db");
    fireEvent.click(within(card).getByRole("button", { name: "Check this file" }));
    fireEvent.click(await within(card).findByRole("button", { name: "Replace my data with this" }));

    expect(await within(card).findByText(/nothing was changed/)).toBeInTheDocument();
    expect(within(card).queryByText(/An import is waiting/)).toBeNull();
  });
});
