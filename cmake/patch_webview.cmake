# Applied by FetchContent to webview 0.12.0. webview hands WebView2 no environment options, so a
# build has no way to pass browser arguments in code. With this patch, a target that defines
# WEBVIEW_EDGE_ENV_OPTIONS supplies them through ::webview_edge_env_options(); everyone else
# gets the original nullptr. Safe to run again on an already-patched tree.
set(header "${SOURCE_DIR}/core/include/webview/webview.h")
file(READ "${header}" text)

if(text MATCHES "webview_edge_env_options")
  return()
endif()

set(include_old "#include \"WebView2.h\"")
set(include_new "#include \"WebView2.h\"\n\n#ifdef WEBVIEW_EDGE_ENV_OPTIONS\nICoreWebView2EnvironmentOptions *webview_edge_env_options();\n#define WEBVIEW_EDGE_ENV_OPTIONS_ARG ::webview_edge_env_options()\n#else\n#define WEBVIEW_EDGE_ENV_OPTIONS_ARG nullptr\n#endif")
set(call_old "nullptr, userDataFolder, nullptr, m_com_handler);")
set(call_new "nullptr, userDataFolder, WEBVIEW_EDGE_ENV_OPTIONS_ARG, m_com_handler);")

foreach(pair "include" "call")
  string(FIND "${text}" "${${pair}_old}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "patch_webview.cmake: '${${pair}_old}' not found in ${header}")
  endif()
  string(REPLACE "${${pair}_old}" "${${pair}_new}" text "${text}")
endforeach()

file(WRITE "${header}" "${text}")
