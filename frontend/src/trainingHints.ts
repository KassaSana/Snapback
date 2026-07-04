/** Build the offline training command to run after in-app export. */
export const buildPipelineCommand = (outputDir: string) => {
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
