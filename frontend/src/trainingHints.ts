/** Build the offline training command to run after in-app export. */
export const buildPipelineCommand = (outputDir: string) => {
  const quotedDir = `"${outputDir.replace(/"/g, '\\"')}"`;
  return [
    "# Run from your Snapback repo root:",
    "python3 -m ml.pipeline_cli \\",
    `  --output-dir ${quotedDir} \\`,
    "  --skip-export",
    "",
    "# Pipeline writes model.onnx here when XGBoost trains successfully.",
    "# Then click Reload model in Snapback (or restart the app).",
  ].join("\n");
};

export const buildExportSummary = (
  featureCount: number,
  labelCount: number,
  outputDir: string,
) =>
  `Exported ${featureCount} features and ${labelCount} labels to ${outputDir}`;

export const classifierBackendLabel = (backend: string) =>
  backend === "onnx" ? "ONNX" : "Heuristic";
