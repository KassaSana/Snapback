#include "app/ipc_shim.hpp"

#include <nlohmann/json.hpp>

namespace snapback {

std::string build_ipc_shim_script(const std::string& trusted_canonical_url,
                                  const std::string& capability_token, bool debug_build) {
    const auto trusted = nlohmann::json(trusted_canonical_url).dump();
    const auto token = nlohmann::json(capability_token).dump();
    const auto debug = debug_build ? "true" : "false";

    // The canonical URL helpers mirror webview_origin.cpp so a path alias cannot read as a
    // different document in JS than in C++. Values are embedded as JSON string literals.
    return std::string(R"JS(
(function () {
  var TRUSTED = )JS") +
           trusted + R"JS(;
  var TOKEN = )JS" + token +
           R"JS(;
  var DEBUG = )JS" + debug +
           R"JS(;
  var TOKEN_KEY = "__snapbackToken";
  var UNAUTHORIZED = "this page is not allowed to use Snapback's native commands";

  function schemeOf(url) {
    var colon = url.indexOf(":");
    if (colon < 0) return "";
    return url.slice(0, colon).toLowerCase();
  }

  function normalizePathSegments(path) {
    var parts = [];
    var absolute = path.length > 0 && path.charAt(0) === "/";
    path.split("/").forEach(function (segment) {
      if (!segment || segment === ".") return;
      if (segment === "..") {
        if (parts.length) parts.pop();
        return;
      }
      parts.push(segment);
    });
    if (parts.length === 0) return absolute ? "/" : "";
    return "/" + parts.join("/");
  }

  function canonicalDocumentUrl(url) {
    var value = url;
    var cut = value.search(/[?#]/);
    if (cut >= 0) value = value.slice(0, cut);

    var scheme = schemeOf(value);
    if (!scheme) return value.toLowerCase();

    var afterScheme = value.slice(scheme.length + 1);
    if (scheme === "file") {
      var start = 0;
      while (start < afterScheme.length && afterScheme.charAt(start) === "/") start++;
      var path = "/" + afterScheme.slice(start).replace(/\\/g, "/").toLowerCase();
      return "file://" + normalizePathSegments(path);
    }

    start = 0;
    while (start < afterScheme.length && afterScheme.charAt(start) === "/") start++;
    var rest = afterScheme.slice(start);
    var slash = rest.indexOf("/");
    var authority = (slash < 0 ? rest : rest.slice(0, slash)).toLowerCase();
    var restPath = slash < 0 ? "" : rest.slice(slash);
    return scheme + "://" + authority + normalizePathSegments(restPath);
  }

  function isLoopbackAuthority(canonical) {
    if (canonical.indexOf("http://") !== 0) return false;
    var rest = canonical.slice("http://".length);
    var slash = rest.indexOf("/");
    var authority = slash < 0 ? rest : rest.slice(0, slash);
    var colon = authority.indexOf(":");
    if (colon >= 0) authority = authority.slice(0, colon);
    return authority === "localhost" || authority === "127.0.0.1" || authority === "[::1]";
  }

  function isTrustedDocument(url) {
    if (!url || !TRUSTED) return false;
    var canonical = canonicalDocumentUrl(url);
    if (canonical === TRUSTED) return true;
    return DEBUG && isLoopbackAuthority(canonical);
  }

  function classifyNavigation(target) {
    if (!target) return "block";
    if (isTrustedDocument(target)) return "allow";
    var scheme = schemeOf(canonicalDocumentUrl(target));
    if (scheme === "http" || scheme === "https" || scheme === "mailto") return "external";
    return "block";
  }

  var callbacks = {};
  var nextCallbackId = 1;
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
    if (!isTrustedDocument(window.location.href)) {
      return Promise.reject(new Error(UNAUTHORIZED));
    }
    args[TOKEN_KEY] = TOKEN;

    var bound = window[cmd];
    if (typeof bound !== "function") {
      return Promise.reject(new Error("unknown command: " + cmd));
    }
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

  document.addEventListener(
    "click",
    function (event) {
      if (!isTrustedDocument(window.location.href)) return;
      var target = event.target;
      if (!target || !target.closest) return;
      var anchor = target.closest("a");
      if (!anchor || !anchor.href) return;
      var decision = classifyNavigation(anchor.href);
      if (decision === "external") {
        event.preventDefault();
        if (typeof window.open_external_url === "function") {
          window.open_external_url({ url: anchor.href });
        }
      } else if (decision === "block") {
        event.preventDefault();
      }
    },
    true,
  );
})();
)JS";
}

}  // namespace snapback
