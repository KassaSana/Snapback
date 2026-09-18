(function () {
  if (window.__snapbackAcceptanceInstalled) return;
  window.__snapbackAcceptanceInstalled = true;

  function assert(condition, message) {
    if (!condition) throw new Error(message);
  }

  async function run() {
    var checks = [];
    var sessionId = null;

    async function check(name, action) {
      try {
        await action();
        checks.push({ name: name, passed: true });
      } catch (error) {
        checks.push({
          name: name,
          passed: false,
          error: error instanceof Error ? error.message : String(error),
        });
      }
    }

    await check("health", async function () {
      var health = await window.__snapback.invoke("get_health");
      assert(health && typeof health === "object", "health did not return an object");
      assert(typeof health.status === "string", "health.status is missing");
    });

    await check("start-session", async function () {
      var session = await window.__snapback.invoke("start_session", {
        goal: "Desktop acceptance round trip",
        focusMode: "normal",
      });
      assert(session && typeof session.sessionId === "string", "start returned no session id");
      sessionId = session.sessionId;
    });

    await check("stop-session", async function () {
      assert(sessionId, "start-session did not produce an id");
      var stopped = await window.__snapback.invoke("stop_session", { sessionId: sessionId });
      assert(stopped && stopped.status === "COMPLETED", "session did not stop cleanly");
    });

    await check("async-export", async function () {
      var result = await window.__snapback.invoke("export_support_bundle");
      assert(result && typeof result.outputPath === "string", "export returned no path");
      assert(result.outputPath.length > 0, "export path was empty");
    });

    await check("error-envelope", async function () {
      var rejected = false;
      try {
        await window.__snapback.invoke("get_session", {
          sessionId: "acceptance-session-that-does-not-exist",
        });
      } catch (error) {
        rejected = error instanceof Error && error.message.indexOf("session not found") >= 0;
      }
      assert(rejected, "a deliberate native error did not reject the page promise");
    });

    var verdict = {
      version: 1,
      passed: checks.every(function (item) {
        return item.passed;
      }),
      checks: checks,
    };

    // Keep the window alive long enough for the Windows smoke to observe its title. The
    // verdict command schedules termination only after its promise has been resolved.
    await new Promise(function (resolve) {
      setTimeout(resolve, 1000);
    });
    await window.__snapback.invoke("report_acceptance_verdict", { verdict: verdict });
  }

  function start() {
    run().catch(function (error) {
      if (window.console && console.error) {
        console.error("snapback desktop acceptance failed before reporting", error);
      }
    });
  }

  if (document.readyState === "complete") {
    setTimeout(start, 0);
  } else {
    window.addEventListener("load", start, { once: true });
  }
})();
