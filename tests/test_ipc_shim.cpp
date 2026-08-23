#include "doctest_wrapper.hpp"

#include "app/ipc_shim.hpp"
#include "app/webview_origin.hpp"

using namespace snapback;

TEST_CASE("build_ipc_shim_script embeds the trusted URL, token, and debug flag") {
    const std::string trusted = "file:///tmp/snapback/frontend/index.html";
    const std::string token =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const auto script = build_ipc_shim_script(trusted, token, true);

    CHECK(script.find(trusted) != std::string::npos);
    CHECK(script.find(token) != std::string::npos);
    CHECK(script.find("var DEBUG = true;") != std::string::npos);
    CHECK(script.find("__snapbackToken") != std::string::npos);
    CHECK(script.find("canonicalDocumentUrl") != std::string::npos);
    CHECK(script.find("open_external_url") != std::string::npos);
}

TEST_CASE("the click interceptor opens external links through the tokened invoke path") {
    // AUD-03. The interceptor called window.open_external_url({ url }) directly, with no
    // capability token, so run_json_command refused it and the click -- already
    // preventDefault()ed -- went nowhere. Routing through invoke() is what attaches the
    // token.
    //
    // This suite tests the emitted script as text, since there is no JS engine here to run
    // it in. So the assertion is that the interceptor's call site goes through invoke():
    // the token attachment itself is invoke()'s single responsibility and is asserted
    // above.
    const auto script = build_ipc_shim_script("file:///tmp/index.html", "tok", false);
    CHECK(script.find(R"(invoke("open_external_url", { url: anchor.href }))") !=
          std::string::npos);
    // The raw binding must not be called with a bare payload anywhere.
    CHECK(script.find("window.open_external_url({ url: anchor.href })") == std::string::npos);
}

TEST_CASE("build_ipc_shim_script disables loopback trust in release mode") {
    const auto script =
        build_ipc_shim_script("file:///tmp/index.html", "abc", false);
    CHECK(script.find("var DEBUG = false;") != std::string::npos);
}

TEST_CASE("build_ipc_shim_script escapes embedded JSON string literals safely") {
    const std::string trusted = R"(file:///tmp/"quoted"/index.html)";
    const std::string token = "tok\"en";
    const auto script = build_ipc_shim_script(trusted, token, false);
    CHECK(script.find("var TRUSTED = \"file:///tmp/\\\"quoted\\\"/index.html\";") !=
          std::string::npos);
    CHECK(script.find("var TOKEN = \"tok\\\"en\";") != std::string::npos);
}
