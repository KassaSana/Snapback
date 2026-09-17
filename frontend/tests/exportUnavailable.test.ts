import assert from "node:assert/strict";

import { api } from "../src/api";

type Json = Record<string, unknown>;

const refusal = {
  ok: false,
  cancelled: true,
  supported: false,
  message: "This writes a file, so it is disabled in the browser demo.",
};

const successFor = (command: string): Json => {
  switch (command) {
    case "export_my_data":
      return {
        outputPath: "/tmp/snapback_my_data.md",
        sessionCount: 3,
        windowCount: 41,
        episodeCount: 2,
        omittedSessions: 0,
        omittedWindows: 0,
        truncated: false,
        checksum: "abc",
      };
    case "export_summary_report":
      return { window: "day", outputPath: "/tmp/summary_day.json" };
    case "export_support_bundle":
      return { outputPath: "/tmp/snapback-support.json", privacyNotice: "Review." };
    case "export_training_data":
      return {
        outputDir: "/tmp/t",
        featuresPath: "f",
        labelsPath: "l",
        featureCount: 9,
        labelCount: 4,
      };
    default:
      throw new Error(`unexpected command ${command}`);
  }
};

const installBridge = (mode: "refuse" | "succeed") => {
  (globalThis as unknown as { window: unknown }).window = {
    __snapback: {
      invoke: async (command: string) => (mode === "refuse" ? { ...refusal } : successFor(command)),
      listen: async () => () => {},
    },
  };
};

// A refusal shape must never map to successful defaults ("wrote 0 sessions, complete
// history", "Exported day summary"). It rejects with the backend's own message, which
// each caller already renders through its failure path.
installBridge("refuse");
await assert.rejects(api.exportMyData(), /disabled in the browser demo/);
await assert.rejects(api.exportSummaryReport({ window: "day" }), /disabled in the browser demo/);
await assert.rejects(api.exportSupportBundle(), /disabled in the browser demo/);
await assert.rejects(api.exportTrainingData(), /disabled in the browser demo/);

// Native-shaped success still resolves through the same mappers.
installBridge("succeed");
assert.equal((await api.exportMyData()).sessionCount, 3);
assert.equal((await api.exportSummaryReport({ window: "day" })).window, "day");
assert.equal((await api.exportSupportBundle()).outputPath, "/tmp/snapback-support.json");
assert.equal((await api.exportTrainingData()).featureCount, 9);

console.log("exportUnavailable.test.ts passed");
