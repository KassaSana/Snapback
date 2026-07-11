// WebDriver end-to-end happy path for the *built* Snapback desktop app.
//
// Unlike the Vitest component tests (which mock the Tauri boundary), this drives
// the real compiled binary through tauri-driver → the platform WebDriver. It's
// the only test that exercises app boot, IPC wiring, asset embedding, and the
// real window — the layers unit/mocked tests can't reach (see docs/TEST_BACKLOG.md #6).
//
// Prerequisites and run instructions: e2e/README.md.

import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { platform } from "node:os";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { expect } from "chai";
import { Builder, By, until } from "selenium-webdriver";

const here = fileURLToPath(new URL(".", import.meta.url));
const repoRoot = resolve(here, "..");

// The release binary produced by `npm run tauri build`. Override with SNAPBACK_BIN.
const binaryName = platform() === "win32" ? "snapback.exe" : "snapback";
const application =
  process.env.SNAPBACK_BIN ??
  resolve(repoRoot, "src-tauri", "target", "release", binaryName);

// tauri-driver from `cargo install tauri-driver`. Override with TAURI_DRIVER.
const home = process.env.USERPROFILE ?? process.env.HOME ?? "";
const tauriDriverPath =
  process.env.TAURI_DRIVER ??
  resolve(home, ".cargo", "bin", platform() === "win32" ? "tauri-driver.exe" : "tauri-driver");

const WEBDRIVER_URL = "http://127.0.0.1:4444/";
const WAIT_MS = 30000;

let driver;
let tauriDriver;

const byButton = (text) => By.xpath(`//button[normalize-space()='${text}']`);
const bySessionStatus = (text) =>
  By.xpath(`//span[contains(@class,'session-status') and normalize-space()='${text}']`);

/** The first-run wizard modal covers the UI when capture isn't confirmed (as on
 *  a fresh CI machine). Dismiss it so the dashboard is interactable. */
async function dismissFirstRunWizard() {
  try {
    const skip = await driver.wait(until.elementLocated(byButton("Skip for now")), 8000);
    await skip.click();
  } catch {
    // No wizard shown (capture already confirmed) — nothing to dismiss.
  }
}

describe("Snapback session happy path (WebDriver E2E)", function () {
  this.timeout(120000);

  before(async function () {
    if (!existsSync(application)) {
      throw new Error(
        `Built app not found at ${application}. Run \`npm run tauri build\` first, or set SNAPBACK_BIN.`,
      );
    }
    if (!existsSync(tauriDriverPath)) {
      throw new Error(
        `tauri-driver not found at ${tauriDriverPath}. Run \`cargo install tauri-driver --locked\`, or set TAURI_DRIVER.`,
      );
    }

    tauriDriver = spawn(tauriDriverPath, [], {
      stdio: [null, process.stdout, process.stderr],
    });
    // Give tauri-driver a moment to bind its port before connecting.
    await new Promise((r) => setTimeout(r, 2000));

    driver = await new Builder()
      .withCapabilities({
        browserName: "wry",
        "tauri:options": { application },
      })
      .usingServer(WEBDRIVER_URL)
      .build();
  });

  after(async function () {
    if (driver) {
      await driver.quit();
    }
    if (tauriDriver) {
      tauriDriver.kill();
    }
  });

  it("renders the dashboard", async function () {
    const heading = await driver.wait(
      until.elementLocated(By.xpath("//h2[normalize-space()='Session Control']")),
      WAIT_MS,
    );
    expect(await heading.getText()).to.equal("Session Control");
  });

  it("starts a session with a goal", async function () {
    await dismissFirstRunWizard();

    const goal = await driver.findElement(
      By.css('input[placeholder="Ship the snapback overlay"]'),
    );
    await goal.clear();
    await goal.sendKeys("E2E happy path");

    await driver.findElement(byButton("Start session")).click();

    // The status pill reflects the active session (warn-don't-block: it starts
    // even if capture permissions are unavailable on the runner).
    await driver.wait(until.elementLocated(bySessionStatus("active")), WAIT_MS);
  });

  it("stops the session and shows it completed", async function () {
    await driver.findElement(byButton("Stop session")).click();
    await driver.wait(until.elementLocated(bySessionStatus("completed")), WAIT_MS);
  });
});
