import assert from "node:assert/strict";

import { buildExportSummary, buildPipelineCommand } from "../src/trainingHints";

const outputDir = "/Users/me/Library/Application Support/com.snapback.app/exports/training";

assert.equal(
  buildExportSummary(12, 3, outputDir),
  "Exported 12 features and 3 labels to /Users/me/Library/Application Support/com.snapback.app/exports/training",
);

const command = buildPipelineCommand(outputDir);
assert.match(command, /^# Run from your Snapback repo root:/);
assert.match(command, /python3 -m ml\.pipeline_cli/);
assert.match(command, /--skip-export/);
assert.match(
  command,
  /--output-dir "\/Users\/me\/Library\/Application Support\/com\.snapback\.app\/exports\/training"/,
);

console.log("trainingHints.test.ts passed");
