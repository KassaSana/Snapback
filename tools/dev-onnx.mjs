#!/usr/bin/env node
/**
 * Start `tauri dev --features onnx` with ORT_DYLIB_PATH set on Windows.
 *
 * Pyke's prebuilt ort-sys package only ships onnxruntime.lib (static). With
 * `load-dynamic`, Rust loads onnxruntime.dll at runtime — we point at the DLL
 * from `pip install onnxruntime` (same runtime used for Python training eval).
 */
import { spawn, execSync } from "node:child_process";
import { existsSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function resolveOnnxDll() {
  if (process.env.ORT_DYLIB_PATH && existsSync(process.env.ORT_DYLIB_PATH)) {
    return process.env.ORT_DYLIB_PATH;
  }

  if (process.env.ONNXRUNTIME_DLL && existsSync(process.env.ONNXRUNTIME_DLL)) {
    return process.env.ONNXRUNTIME_DLL;
  }

  const pyCommands = ["python", "python3"];
  if (process.platform === "win32") {
    pyCommands.push("py -3");
  }

  for (const py of pyCommands) {
    try {
      const out = execSync(
        `${py} -c "import onnxruntime, pathlib; print(pathlib.Path(onnxruntime.__file__).parent / 'capi' / 'onnxruntime.dll')"`,
        { encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] },
      ).trim();
      if (out && existsSync(out)) {
        return out;
      }
    } catch {
      // try next python
    }
  }

  return null;
}

const dll = resolveOnnxDll();
const env = { ...process.env };

if (dll) {
  env.ORT_DYLIB_PATH = dll;
  console.log(`Using ONNX Runtime DLL: ${dll}`);
} else if (process.platform === "win32") {
  console.warn(
    "Warning: onnxruntime.dll not found. Install with: pip install onnxruntime",
  );
  console.warn("Or run: npm run tauri:dev:heuristic");
}

const child = spawn("npx", ["tauri", "dev", "--features", "onnx"], {
  stdio: "inherit",
  shell: true,
  env,
  cwd: repoRoot,
});

child.on("exit", (code) => {
  process.exit(code ?? 1);
});
