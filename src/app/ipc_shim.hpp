// The JavaScript IPC shim injected into the webview before the app bundle loads.
// webview.bind() exposes native commands, while this small bridge gives the frontend a
// typed invoke/listen surface and a host-to-frontend event bus.
//
// How this meets webview: webview.bind("name", fn) exposes window["name"](...args) that
// returns a Promise and delivers the args to C++ as a JSON *array* string. So invoke()
// forwards `window[cmd](args)` -> the C++ handler receives "[{...args}]" and reads [0].
//
// Error channel: a synchronous bind can only *resolve* a Promise, never reject one. We
// use an envelope: a handler that fails returns {"__snapback_error":"msg"}; invoke()
// below inspects the resolved value and rejects on that key.
#pragma once

namespace snapback {

// Injected via webview.init(...), which runs on every navigation BEFORE page scripts.
inline constexpr const char* kIpcShim = R"JS(
(function () {
  // Registry of frontend callbacks. The event bus dispatches to them later.
  var callbacks = {};
  var nextCallbackId = 1;

  // event name -> Set of callback ids listening on it.
  var listeners = {};

  function listen(event, cb) {
    var id = nextCallbackId++;
    callbacks[id] = cb;
    (listeners[event] = listeners[event] || {})[id] = true;
    return Promise.resolve(function () {
      delete callbacks[id];
      if (listeners[event]) delete listeners[event][id];
    });
  }

  function invoke(cmd, args) {
    args = args || {};

    // Forward to the matching webview.bind()-exposed function.
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

  window.__snapback = {
    invoke: invoke,
    listen: listen,
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
