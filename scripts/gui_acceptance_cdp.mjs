#!/usr/bin/env node

// Dependency-free WebView2 driver for Roadmap 10.1. WebView2 exposes the Chromium DevTools
// Protocol when the smoke process supplies --remote-debugging-port. Node 22 supplies fetch
// and WebSocket, so CI does not need a browser download merely to attach to the app's browser.

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function argument(name, fallback) {
  const index = process.argv.indexOf(name);
  return index >= 0 && process.argv[index + 1]
    ? process.argv[index + 1]
    : fallback;
}

const port = Number(argument("--port", "9222"));
const timeoutMs = Number(argument("--timeout-ms", "20000"));
if (!Number.isInteger(port) || port < 1 || port > 65535) {
  throw new Error(`invalid --port: ${port}`);
}
if (!Number.isFinite(timeoutMs) || timeoutMs < 1000) {
  throw new Error(`invalid --timeout-ms: ${timeoutMs}`);
}

async function findSnapbackTarget() {
  const deadline = Date.now() + timeoutMs;
  let lastError = null;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/json`);
      if (!response.ok)
        throw new Error(`CDP target list returned HTTP ${response.status}`);
      const targets = await response.json();
      const target = targets.find(
        (candidate) =>
          candidate.type === "page" &&
          candidate.title.includes("Snapback") &&
          candidate.webSocketDebuggerUrl,
      );
      if (target) return target;
    } catch (error) {
      lastError = error;
    }
    await sleep(200);
  }
  throw new Error(
    `Snapback did not expose a CDP page on port ${port}: ${lastError?.message ?? "no target"}`,
  );
}

class CdpConnection {
  constructor(url) {
    this.socket = new WebSocket(url);
    this.nextId = 1;
    this.pending = new Map();
  }

  async open() {
    await new Promise((resolve, reject) => {
      this.socket.addEventListener("open", resolve, { once: true });
      this.socket.addEventListener(
        "error",
        () => reject(new Error("failed to connect to the WebView2 CDP socket")),
        { once: true },
      );
    });
    this.socket.addEventListener("message", (event) => {
      const message = JSON.parse(event.data);
      if (!message.id) return;
      const pending = this.pending.get(message.id);
      if (!pending) return;
      this.pending.delete(message.id);
      if (message.error) pending.reject(new Error(message.error.message));
      else pending.resolve(message.result);
    });
    this.socket.addEventListener("close", () => {
      for (const pending of this.pending.values()) {
        pending.reject(
          new Error("WebView2 closed the CDP socket before replying"),
        );
      }
      this.pending.clear();
    });
  }

  call(method, params = {}) {
    const id = this.nextId++;
    const response = new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
    });
    this.socket.send(JSON.stringify({ id, method, params }));
    return response;
  }

  close() {
    this.socket.close();
  }
}

const driverProgram = String.raw`
(async () => {
  const checks = [];
  const acceptanceDeadline = Date.now() + 12000;
  const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const waitFor = async (description, predicate) => {
    while (Date.now() < acceptanceDeadline) {
      const value = predicate();
      if (value) return value;
      await pause(100);
    }
    throw new Error("timed out waiting for " + description);
  };
  const buttonNamed = (name) =>
    Array.from(document.querySelectorAll("button")).find(
      (button) => button.textContent.trim() === name,
    );
  const check = async (name, action) => {
    try {
      await action();
      checks.push({ name, passed: true });
    } catch (error) {
      checks.push({
        name,
        passed: false,
        error: error instanceof Error ? error.message : String(error),
      });
    }
  };

  const skip = buttonNamed("Skip for now");
  if (skip) skip.click();

  await check("app-shell-ready", async () => {
    await waitFor("the native bridge", () => window.__snapback?.invoke);
    await waitFor("the driven-acceptance bootstrap", () => window.__snapbackDrivenAcceptanceReady);
    await waitFor("the Now tab", () => document.querySelector("#surface-tab-now"));
  });

  await check("navigate-review", async () => {
    const tab = await waitFor("the Review tab", () => document.querySelector("#surface-tab-review"));
    tab.click();
    await waitFor("the Review panel", () => tab.getAttribute("aria-selected") === "true");
  });

  await check("navigate-settings", async () => {
    const tab = await waitFor("the Settings tab", () => document.querySelector("#surface-tab-settings"));
    tab.click();
    await waitFor("the Settings panel", () => tab.getAttribute("aria-selected") === "true");
  });

  await check("start-session", async () => {
    const now = await waitFor("the Now tab", () => document.querySelector("#surface-tab-now"));
    now.click();
    await waitFor("the Now panel", () => now.getAttribute("aria-selected") === "true");
    const input = await waitFor("the focus goal input", () =>
      document.querySelector('input[placeholder="Ship the snapback overlay"]'),
    );
    const valueSetter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set;
    valueSetter.call(input, "Driven desktop acceptance");
    input.dispatchEvent(new Event("input", { bubbles: true }));
    const start = await waitFor("an enabled Start session button", () => {
      const candidate = buttonNamed("Start session");
      return candidate && !candidate.disabled ? candidate : null;
    });
    start.click();
    await waitFor("the Stop session button", () => buttonNamed("Stop session"));
  });

  await check("stop-session", async () => {
    const stop = await waitFor("the Stop session button", () => buttonNamed("Stop session"));
    stop.click();
    await waitFor("the restored Start session button", () => buttonNamed("Start session"));
  });

  const verdict = {
    version: 1,
    driver: "webview2-cdp",
    passed: checks.every((item) => item.passed),
    checks,
  };
  await window.__snapback.invoke("report_acceptance_verdict", { verdict });
  return verdict;
})()
`;

const target = await findSnapbackTarget();
const cdp = new CdpConnection(target.webSocketDebuggerUrl);
await cdp.open();
try {
  await cdp.call("Runtime.enable");
  const evaluation = await cdp.call("Runtime.evaluate", {
    expression: driverProgram,
    awaitPromise: true,
    returnByValue: true,
  });
  if (evaluation.exceptionDetails) {
    throw new Error(
      evaluation.exceptionDetails.exception?.description ??
        evaluation.exceptionDetails.text ??
        "the driven acceptance program threw",
    );
  }
  const verdict = evaluation.result?.value;
  if (!verdict || !Array.isArray(verdict.checks)) {
    throw new Error(
      "the driven acceptance program returned no structured verdict",
    );
  }
  console.log(JSON.stringify(verdict));
  if (!verdict.passed) process.exitCode = 1;
} finally {
  cdp.close();
}
