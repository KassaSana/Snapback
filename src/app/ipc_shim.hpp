// The JavaScript IPC shim injected into the webview before the app bundle loads.
// Rust/Tauri: this glue is auto-generated + injected by the framework. webview/webview
// has none, so we hand-write the tiny slice of Tauri v2's runtime the frontend uses.
//
// The reused React app imports @tauri-apps/api v2, whose ENTIRE surface reduces to two
// window.__TAURI_INTERNALS__ methods:
//   - invoke(cmd, args, options) -> Promise         (core.js)
//   - transformCallback(cb, once) -> numeric id     (core.js)
// and events (event.js) are built on top of invoke via the pseudo-commands
// "plugin:event|listen" / "|unlisten" / "|emit". So if we implement those two methods
// (plus an event dispatch entry point the host calls), the unmodified npm package works.
//
// How this meets webview: webview.bind("name", fn) exposes window["name"](...args) that
// returns a Promise and delivers the args to C++ as a JSON *array* string. So invoke()
// forwards `window[cmd](args)` -> the C++ handler receives "[{...args}]" and reads [0].
//
// Error channel: a synchronous bind can only *resolve* a Promise, never reject one. We
// adopt an envelope: a handler that fails returns {"__snapback_error":"msg"}; invoke()
// below inspects the resolved value and rejects on that key, so the frontend's .catch()
// still fires (e.g. blank-goal validation) exactly as it did against Rust's Result::Err.
#pragma once

namespace snapback {

// Injected via webview.init(...), which runs on every navigation BEFORE page scripts —
// so __TAURI_INTERNALS__ exists by the time the app bundle's imports run.
inline constexpr const char* kIpcShim = R"JS(
(function () {
  // Registry of frontend callbacks (event.listen handlers). transformCallback stores a
  // callback under a numeric id and returns it; the event bus dispatches to it later.
  var callbacks = {};
  var nextCallbackId = 1;

  // event name -> Set of callback ids listening on it.
  var listeners = {};

  function transformCallback(cb, once) {
    var id = nextCallbackId++;
    callbacks[id] = function (payload) {
      if (once) delete callbacks[id];
      return cb(payload);
    };
    return id;
  }

  function invoke(cmd, args) {
    args = args || {};

    // --- Event plugin: listen/unlisten/emit are modelled locally. ---
    if (cmd === "plugin:event|listen") {
      var event = args.event;
      var handlerId = args.handler; // already a transformCallback id
      (listeners[event] = listeners[event] || {})[handlerId] = true;
      return Promise.resolve(handlerId); // returned as the eventId for unlisten
    }
    if (cmd === "plugin:event|unlisten") {
      var ev = args.event;
      if (listeners[ev]) delete listeners[ev][args.eventId];
      return Promise.resolve();
    }
    if (cmd === "plugin:event|emit" || cmd === "plugin:event|emit_to") {
      // Frontend -> host events are unused in this app; accept and drop.
      return Promise.resolve();
    }

    // --- Real commands: forward to the matching webview.bind()-exposed function. ---
    var bound = window[cmd];
    if (typeof bound !== "function") {
      return Promise.reject(new Error("unknown command: " + cmd));
    }
    // webview delivers `args` to C++ as the single element of a JSON array.
    return Promise.resolve(bound(args)).then(function (result) {
      if (result && typeof result === "object" && "__snapback_error" in result) {
        return Promise.reject(new Error(result.__snapback_error));
      }
      return result;
    });
  }

  window.__TAURI_INTERNALS__ = {
    invoke: invoke,
    transformCallback: transformCallback,
    // A couple of no-op stubs the api package may touch; harmless to provide.
    unregisterCallback: function (id) { delete callbacks[id]; },
    convertFileSrc: function (p) { return p; },
  };

  // Host -> frontend event delivery. commands.hpp's emit() evals a call to this.
  // Each listener receives Tauri's event shape: { event, id, payload }.
  window.__snapback = {
    emit: function (event, payload) {
      var ids = listeners[event];
      if (!ids) return;
      Object.keys(ids).forEach(function (id) {
        var cb = callbacks[id];
        if (cb) cb({ event: event, id: Number(id), payload: payload });
      });
    },
  };
})();
)JS";

}  // namespace snapback
