import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Mock the Tauri boundary with a stateful rules store so the list round-trips
// through the real api.ts + useAppRules (add pushes, delete removes, get reads).
const boundary = vi.hoisted(() => {
  const state: { health: Record<string, unknown>; rules: Record<string, unknown>[] } = {
    health: {},
    rules: [],
  };
  let nextId = 1;

  const invoke = vi.fn(async (cmd: string, args?: Record<string, unknown>): Promise<unknown> => {
    switch (cmd) {
      case "get_health":
        return state.health;
      case "get_app_rules":
        return state.rules;
      case "upsert_app_rule": {
        const request = (args?.request ?? {}) as Record<string, unknown>;
        const rule = {
          id: nextId++,
          pattern: String(request.pattern ?? ""),
          rule_type: String(request.ruleType ?? "allow"),
          note: request.note ?? null,
          created_at: "2026-07-11T00:00:00Z",
          updated_at: "2026-07-11T00:00:00Z",
        };
        state.rules = [...state.rules, rule];
        return rule;
      }
      case "delete_app_rule": {
        state.rules = state.rules.filter((r) => r.id !== args?.id);
        return null;
      }
      case "get_prediction_history":
      case "get_context_timeline":
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

beforeEach(() => {
  window.localStorage.clear();
  boundary.invoke.mockClear();
  boundary.state.health = healthyCaptureRunning();
  boundary.state.rules = [];
});

afterEach(() => {
  cleanup();
});

describe("App rules add/delete flow", () => {
  it("adds a rule and shows it in the list", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Personal App Rules" });

    fireEvent.change(screen.getByPlaceholderText("discord, notion, youtube"), {
      target: { value: "discord" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Save rule" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("upsert_app_rule", {
        request: { pattern: "discord", ruleType: "allow", note: null },
      }),
    );
    expect(await screen.findByText("discord")).toBeInTheDocument();
    expect(await screen.findByText(/Saved allow rule/i)).toBeInTheDocument();
  });

  it("does not save when the pattern is empty", async () => {
    render(<App />);
    await screen.findByRole("heading", { name: "Personal App Rules" });

    fireEvent.click(screen.getByRole("button", { name: "Save rule" }));

    expect(await screen.findByText(/Enter an app name or keyword/i)).toBeInTheDocument();
    expect(boundary.invoke).not.toHaveBeenCalledWith("upsert_app_rule", expect.anything());
  });

  it("removes a rule from the list", async () => {
    boundary.state.rules = [
      {
        id: 7,
        pattern: "notion",
        rule_type: "block",
        note: null,
        created_at: "2026-07-11T00:00:00Z",
        updated_at: "2026-07-11T00:00:00Z",
      },
    ];
    render(<App />);
    expect(await screen.findByText("notion")).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Remove" }));

    await waitFor(() =>
      expect(boundary.invoke).toHaveBeenCalledWith("delete_app_rule", { id: 7 }),
    );
    await waitFor(() => expect(screen.queryByText("notion")).not.toBeInTheDocument());
  });
});
