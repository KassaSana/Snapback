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

/** Set a React controlled input's value so onChange fires (see call site).
 *  Resets React's internal `_valueTracker` so React is forced to register the
 *  change even if the tracker already holds a value. */
async function setReactInputValue(element, value) {
  await driver.executeScript(
    "const input = arguments[0], value = arguments[1];" +
      "const setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;" +
      "setter.call(input, value);" +
      "if (input._valueTracker) { input._valueTracker.setValue(''); }" +
      "input.dispatchEvent(new Event('input', { bubbles: true }));",
    element,
    value,
  );
}
const bySessionStatus = (text) =>
  By.xpath(`//span[contains(@class,'session-status') and normalize-space()='${text}']`);

/** The App health pill text ("checking" until the first get_health resolves). */
async function appHealthText() {
  try {
    return await driver
      .findElement(
        By.xpath(
          "//span[@class='status-label' and normalize-space()='App']/following-sibling::span[contains(@class,'status-value')]",
        ),
      )
      .getAttribute("textContent");
  } catch {
    return null;
  }
}

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

    // The app is launched by tauri-driver and inherits this env: E2E mode skips
    // global input capture / hotkeys, which otherwise wedge the app under
    // automation (global OS hooks vs. WebDriver).
    tauriDriver = spawn(tauriDriverPath, [], {
      stdio: [null, process.stdout, process.stderr],
      env: { ...process.env, SNAPBACK_E2E: "1" },
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

    // Fail fast instead of hanging on the driver's 5-min renderer timeout.
    await driver.manage().setTimeouts({ script: 20000, pageLoad: 30000 });

    // Wait for the shell, then clear the first-run wizard so the dashboard is
    // visible and interactable for every test below.
    await driver.wait(
      until.elementLocated(By.xpath("//h2[normalize-space()='Session Control']")),
      WAIT_MS,
    );
    // Wait until the app is actually interactive: the first health load has
    // resolved (status leaves "checking"). Interacting before React is ready is
    // racy under automation — inputs/clicks silently no-op.
    await driver.wait(
      async () => {
        const t = await appHealthText();
        return t !== null && t !== "checking";
      },
      WAIT_MS,
      "app never became interactive (health stuck on 'checking')",
    );
    await dismissFirstRunWizard();
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
    const heading = await driver.findElement(
      By.xpath("//h2[normalize-space()='Session Control']"),
    );
    // textContent is robust to the element being visually covered.
    expect(await heading.getAttribute("textContent")).to.contain("Session Control");
  });

  it("starts a session with a goal", async function () {
    const goal = await driver.findElement(
      By.css('input[placeholder="Ship the snapback overlay"]'),
    );
    await goal.click();
    // Set the value through React's tracked native setter and fire an `input`
    // event, so the controlled component's onChange runs and `sessionGoal`
    // state updates. Plain `sendKeys` sets only the DOM value, which React's
    // value tracker ignores — the input *looks* filled but state stays empty,
    // so Start would no-op on a "blank" goal.
    await setReactInputValue(goal, "E2E happy path");
    await driver.sleep(300); // let React commit the state update before clicking

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
