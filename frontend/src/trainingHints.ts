/** Build the offline training command to run after in-app export. */
export const buildPipelineCommand = (outputDir: string, pipelineCommand?: string) => {
  if (pipelineCommand) {
    return pipelineCommand;
  }
  const quotedDir = `"${outputDir.replace(/"/g, '\\"')}"`;
  return [
    "# Run from your Snapback repo root:",
    "python3 -m ml.pipeline_cli \\",
    `  --output-dir ${quotedDir} \\`,
    "  --skip-export",
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

export const formatTrainingMetrics = (metrics: Record<string, number> | null) => {
  if (!metrics) {
    return null;
  }
  const accuracy = metrics.cv_accuracy ?? metrics.in_sample_accuracy;
  const precision = metrics.precision_at_10pct ?? metrics.in_sample_precision_at_10pct;
  const recall = metrics.recall_distracted ?? metrics.in_sample_recall_distracted;
  const parts: string[] = [];
  if (accuracy !== undefined) {
    parts.push(`accuracy ${(accuracy * 100).toFixed(1)}%`);
  }
  if (precision !== undefined) {
    parts.push(`precision@10% ${(precision * 100).toFixed(1)}%`);
  }
  if (recall !== undefined) {
    parts.push(`recall distracted ${(recall * 100).toFixed(1)}%`);
  }
  return parts.length > 0 ? parts.join(" · ") : null;
};
