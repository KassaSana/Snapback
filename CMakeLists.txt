cmake_minimum_required(VERSION 3.20)
project(snapback_cpp VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Roadmap 4.12. Emit compile_commands.json so clangd and clang-tidy see the same flags the
# build uses, and so `.clang-tidy`'s check set applies in every editor rather than each
# developer's defaults. It is a generator artifact, already gitignored, and costs nothing to
# produce. Multi-config generators (MSVC) ignore this; Ninja and Make honour it.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  # /utf-8: source + exec charset UTF-8 (our string literals contain UTF-8 bytes).
  # /W4: high warning level. /permissive-: standards-conformant mode.
  add_compile_options(/utf-8 /W4 /permissive- /EHsc)
endif()

include(FetchContent)

# Vendored deps (e.g. doctest v2.4.11) still declare cmake_minimum_required(VERSION <3.5).
# CMake 4.x removed compatibility with that floor, so FetchContent's sub-configure hard-fails
# on newer runner images (GitHub's windows-latest/macos-latest) while older CMake (Linux,
# local) only warns. This raises the effective policy floor for those subprojects without
# patching their files — the durable form of CMake's own "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
# suggestion. Our own cmake_minimum_required above (3.20) is unaffected.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# --- Always fetched: header-only deps used by the core lib and the tests. ---
#
# ROADMAP 8.6: every GIT_TAG here is a commit SHA, never a tag name. A git tag is a mutable
# pointer — upstream can move it, and a fetched dependency is source we compile into the
# shipping binary without review. Dependabot watches our Actions and our npm tree; it does
# not watch these, so nothing else would notice. `scripts/check_dependency_pins.py` fails CI
# if a pin here is anything but a 40-character hash, and `docs/dependencies.md` records how
# to bump one. The human-readable version stays in the comment beside each pin, because a
# bare hash tells a reader nothing.
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_TAG        9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03  # v3.11.3
  GIT_SHALLOW    TRUE)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest
  GIT_TAG        ae7a13539fb71f270b87eb2e874fbac80bc8dda2  # v2.4.11
  GIT_SHALLOW    TRUE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json doctest)

# The full desktop app (webview + WebView2 + sqlite) is heavy and OS-bound. Keep it
# OFF during the early, pure-logic phases so the test suite builds without it. We
# flip this ON in Phase 6 when the webview IPC bridge lands.
option(SNAPBACK_BUILD_APP "Build the desktop app target (webview + sqlite)" OFF)
option(SNAPBACK_ONNX      "Build with ONNX Runtime inference" OFF)
option(SNAPBACK_BUILD_BENCHMARKS "Build standalone performance benchmarks" OFF)

# SQLite amalgamation. Prefer a local vendored copy when present
# so offline development works; otherwise fetch a pinned upstream amalgamation for CI.
set(SNAPBACK_SQLITE_LOCAL_DIR "${CMAKE_SOURCE_DIR}/third_party/sqlite")
if(EXISTS "${SNAPBACK_SQLITE_LOCAL_DIR}/sqlite3.c" AND
   EXISTS "${SNAPBACK_SQLITE_LOCAL_DIR}/sqlite3.h")
  add_library(sqlite3 STATIC "${SNAPBACK_SQLITE_LOCAL_DIR}/sqlite3.c")
  target_include_directories(sqlite3 PUBLIC "${SNAPBACK_SQLITE_LOCAL_DIR}")
else()
  FetchContent_Declare(sqlite_amalgamation
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3450300.zip
    URL_HASH SHA256=EA170E73E447703E8359308CA2E4366A3AE0C4304A8665896F068C736781C651)
  FetchContent_MakeAvailable(sqlite_amalgamation)
  add_library(sqlite3 STATIC "${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c")
  target_include_directories(sqlite3 PUBLIC "${sqlite_amalgamation_SOURCE_DIR}")
endif()

# --- snapback_core: portable logic shared by the app and the tests. ---
# Grows one phase at a time (kept explicit, not globbed, so each phase's additions
# are deliberate and reviewable).
add_library(snapback_core STATIC
  src/types.cpp
  src/engine/app_context.cpp
  src/engine/classifier.cpp
  src/engine/feature_parity.cpp
  src/engine/features.cpp
  src/engine/onnx_model.cpp
  src/engine/pomodoro.cpp
  src/storage/storage.cpp
  src/snapback/tracker.cpp
  src/snapback/title_parser.cpp
  src/snapback/overlay_common.cpp
  src/app/tray_common.cpp
  src/util/private_dir.cpp
  src/util/durable_file.cpp
  src/app/webview_origin.cpp
)
target_include_directories(snapback_core PUBLIC src)
target_link_libraries(snapback_core PUBLIC nlohmann_json::nlohmann_json sqlite3)

# --- ONNX Runtime (optional backend) ---
# The define is PUBLIC on the core so classifier.cpp AND every consumer (tests + app) see
# the same OnnxModel layout. The platform runtime is staged next to each executable by
# stage_onnxruntime() below so the ONNX tests exercise loading as well as compilation.
if(SNAPBACK_ONNX)
  set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime" CACHE PATH "ONNX Runtime root")
  target_compile_definitions(snapback_core PUBLIC SNAPBACK_ONNX=1)
  target_include_directories(snapback_core PUBLIC ${ONNXRUNTIME_ROOT}/include)

  if(WIN32)
    set(ONNXRUNTIME_LIBRARY "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    set(ONNXRUNTIME_RUNTIME "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
  elseif(APPLE)
    set(ONNXRUNTIME_LIBRARY "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib")
    set(ONNXRUNTIME_RUNTIME "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib")
  else()
    set(ONNXRUNTIME_LIBRARY "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")
    set(ONNXRUNTIME_RUNTIME "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")
  endif()
  if(NOT EXISTS "${ONNXRUNTIME_LIBRARY}" OR NOT EXISTS "${ONNXRUNTIME_RUNTIME}")
    message(FATAL_ERROR
      "SNAPBACK_ONNX requires the platform ONNX Runtime files under ${ONNXRUNTIME_ROOT}/lib")
  endif()
  target_link_libraries(snapback_core PUBLIC "${ONNXRUNTIME_LIBRARY}")

  # Stage the shared runtime next to a target so it is found when tests or the app run.
  function(stage_onnxruntime target)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_RUNTIME}" "$<TARGET_FILE_DIR:${target}>")
    if(APPLE)
      set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "@loader_path")
    elseif(UNIX)
      set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
    endif()
  endfunction()
else()
  function(stage_onnxruntime target)
  endfunction()
endif()

# --- capture backend: compiled in Phase 4, exercised through unit tests where safe. ---
add_library(snapback_capture STATIC
  src/capture/active_window.cpp
  src/capture/capture_thread.cpp
  src/capture/input_hook_posix.cpp
  src/capture/input_hook_windows.cpp
  src/capture/permissions.cpp
)
target_include_directories(snapback_capture PUBLIC src)
target_link_libraries(snapback_capture PUBLIC snapback_core)
if(WIN32)
  target_link_libraries(snapback_capture PUBLIC user32 psapi)
elseif(APPLE)
  target_sources(snapback_capture PRIVATE src/capture/input_hook_macos.mm)
  set_source_files_properties(src/capture/input_hook_macos.mm PROPERTIES COMPILE_FLAGS "-x objective-c++")
  target_link_libraries(snapback_capture PUBLIC "-framework ApplicationServices" "-framework CoreFoundation")
elseif(UNIX)
  target_sources(snapback_capture PRIVATE src/capture/input_hook_linux.cpp)
endif()

# --- app state: ties capture + engine + storage together. ---
add_library(snapback_app STATIC
  src/app/activation_channel.cpp
  src/app/autostart.cpp
  src/app/autostart_launchd.cpp
  src/app/autostart_run_key.cpp
  src/app/autostart_systemd.cpp
  src/app/data_export.cpp
  src/app/data_import.cpp
  src/app/frontend_assets.cpp
  src/app/ipc_shim.cpp
  src/app/open_url.cpp
  src/app/reveal_path.cpp
  src/app/single_instance.cpp
  src/app/settings.cpp
  src/app/uninstall.cpp
  src/app/state.cpp
  src/app/support_bundle.cpp
  src/app/training_deploy.cpp
  # Platform-independent half of focus_window: input validation shared by all three
  # backends. Only focus_window_supported()/focus_window_native() are per-platform.
  src/snapback/focus_window.cpp
)
target_include_directories(snapback_app PUBLIC src)
target_link_libraries(snapback_app PUBLIC snapback_core snapback_capture)
if(WIN32)
  target_sources(snapback_app PRIVATE
    src/snapback/focus_window_windows.cpp
    src/app/file_dialog_windows.cpp
    src/app/window_lifecycle_windows.cpp)
  # comdlg32: the file dialogs (10.14). advapi32: the activation pipe's per-user DACL (9.15).
  target_link_libraries(snapback_app PUBLIC comdlg32 advapi32)

elseif(APPLE)
  # ROADMAP 7.6: the file-manager backend is AppKit, so macOS needs the Objective-C++ half.
  # It lives in the library (not the app target like the tray/overlay) because it has no
  # singleton and no window — the tests link it and exercise its refusal paths.
  target_sources(snapback_app PRIVATE
    src/app/open_url_macos.mm
    src/app/reveal_path_macos.mm
    src/snapback/focus_window_macos.mm
    src/app/file_dialog_macos.mm
    src/app/window_lifecycle_macos.mm)
  set_source_files_properties(
    src/app/open_url_macos.mm src/app/reveal_path_macos.mm src/snapback/focus_window_macos.mm src/app/file_dialog_macos.mm
    PROPERTIES COMPILE_FLAGS "-x objective-c++")
  # window_lifecycle_macos.mm is the one file here written against ARC: it uses `__bridge`
  # casts and a `weak` delegate property, both of which are hard errors under manual
  # retain/release rather than warnings. ARC is a per-translation-unit choice, so enabling it
  # for this file alone is safe and leaves the other four exactly as they were -- none of them
  # uses an ARC-only construct, and none does manual retain/release that ARC would reject.
  #
  # Without this the file cannot compile at all, which took every macOS job down from the
  # commit that added it. Nothing outside macOS builds it, so no other job could report it.
  set_source_files_properties(
    src/app/window_lifecycle_macos.mm
    PROPERTIES COMPILE_FLAGS "-x objective-c++ -fobjc-arc")
  target_link_libraries(snapback_app PUBLIC "-framework AppKit")
else()
  target_sources(snapback_app PRIVATE
    src/snapback/focus_window_stub.cpp
    src/app/file_dialog_stub.cpp
    src/app/window_lifecycle_stub.cpp)
endif()

target_compile_definitions(snapback_app PUBLIC SNAPBACK_VERSION=\"${PROJECT_VERSION}\")



# --- tests (doctest) ---
enable_testing()
file(GLOB SNAPBACK_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(snapback_tests ${SNAPBACK_TEST_SOURCES})
# snapback_app already PUBLIC-links snapback_core + snapback_capture (see above), so they
# propagate transitively — listing them again here just made the linker warn about
# duplicate static libraries.
target_link_libraries(snapback_tests PRIVATE snapback_app doctest::doctest)
# The fixture test loads fixtures/model.onnx; hand the path to the test via a define.
target_compile_definitions(snapback_tests PRIVATE
  SNAPBACK_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/fixtures"
  SNAPBACK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
stage_onnxruntime(snapback_tests)

# ROADMAP 11.1: register every doctest case as its own CTest test rather than the whole
# binary as one.
#
# The reason is 6.1. A single SIGSEGV took the process down and CTest could only report the
# one entry it knew about as failed — the 138 cases that never got to run were reported as
# *skipped*, which reads like "nothing to see here" rather than "we have no idea whether
# these pass." A crash cost us the answer to every question after it, and the missing
# coverage was invisible in exactly the situation where it mattered most.
#
# doctest_discover_tests asks the built binary for its case list and emits one add_test per
# case, so a crash now costs one result. It also makes CI's per-test `--timeout` mean what it
# says: previously the timeout covered all 295 cases at once, so one hung test was
# indistinguishable from a slow suite.
#
# The cost is one process launch per case instead of one for the run. Measured on this Mac:
# 2.4s as a single binary, and the per-case run is the price of not being blind after a
# crash. Cases are also better isolated than before — global state cannot leak between them.
list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")
include(doctest)
doctest_discover_tests(snapback_tests)

add_executable(snapback_feature_parity_export tools/feature_parity_export.cpp)
target_link_libraries(snapback_feature_parity_export PRIVATE snapback_core)

add_executable(snapback_instance_probe tools/single_instance_probe.cpp)
target_link_libraries(snapback_instance_probe PRIVATE snapback_app)
add_dependencies(snapback_tests snapback_instance_probe)
target_compile_definitions(snapback_tests PRIVATE
  SNAPBACK_INSTANCE_PROBE="$<TARGET_FILE:snapback_instance_probe>")

if(SNAPBACK_BUILD_BENCHMARKS)
  add_executable(snapback_benchmarks benchmarks/bench_snapback.cpp)
  # ROADMAP 7.14: the benchmarks drive the same private event seam the tests do, through the
  # same AppStateTestAccess friend. `tests/` is on the include path for that one header —
  # benchmarks are non-shipping harness code, same category as tests, and the alternative is
  # putting a test hook back on the production class's public API.
  target_include_directories(snapback_benchmarks PRIVATE benchmarks tests)
  # snapback_app already PUBLIC-links snapback_core + snapback_capture transitively.
  target_link_libraries(snapback_benchmarks PRIVATE snapback_app)

  # Targeted hot-path micro-benchmarks (producer/consumer/lock/SQLite).
  add_executable(snapback_hotpath_benchmarks benchmarks/bench_hotpaths.cpp)
  target_include_directories(snapback_hotpath_benchmarks PRIVATE benchmarks tests)
  target_link_libraries(snapback_hotpath_benchmarks PRIVATE snapback_app)
endif()

# --- the desktop app (later phases) ---
if(SNAPBACK_BUILD_APP)
  # ROADMAP 8.6. Note this is the *commit* the 0.12.0 tag points at, not the tag object:
  # webview uses an annotated tag, so `git ls-remote refs/tags/0.12.0` answers 782c12cc,
  # which is the tag object and is not a commit CMake can check out. The peeled ref
  # (`refs/tags/0.12.0^{}`) is the commit below. json and doctest use lightweight tags, where
  # the distinction does not arise — which is exactly why it is easy to get wrong here.
  FetchContent_Declare(webview
    GIT_REPOSITORY https://github.com/webview/webview
    GIT_TAG        3ab4b5d722438fc8a13e6ca830c5e2372d19a01d)  # 0.12.0
  FetchContent_MakeAvailable(webview)

  add_executable(snapback
    src/main.cpp
  )
  if(WIN32)
    target_sources(snapback PRIVATE src/snapback/overlay_windows.cpp src/app/tray_windows.cpp)
  elseif(APPLE)
    # ROADMAP 3.1: a real NSStatusItem tray and a real NSPanel overlay. Both stubs also
    # guard themselves on __APPLE__, so listing one here by mistake cannot produce a
    # duplicate Overlay::instance()/Tray::instance().
    target_sources(snapback PRIVATE
      src/snapback/overlay_macos.mm
      src/app/tray_macos.mm
      src/app/mac_ui.mm)
    set_source_files_properties(
      src/snapback/overlay_macos.mm src/app/tray_macos.mm src/app/mac_ui.mm
      PROPERTIES COMPILE_FLAGS "-x objective-c++")
    target_link_libraries(snapback PRIVATE "-framework Cocoa")
  else()
    # Overlay::instance()/Tray::instance() live ONLY in the platform files above, so
    # without these no-op fallbacks the app fails to link (undefined symbols).
    # Real implementation still to come: ROADMAP 3.2 (Linux).
    target_sources(snapback PRIVATE src/snapback/overlay_stub.cpp src/app/tray_stub.cpp)
  endif()
  # snapback_app already PUBLIC-links snapback_core (which itself PUBLIC-links sqlite3)
  # and snapback_capture transitively.
  target_link_libraries(snapback PRIVATE snapback_app webview::core)
  if(WIN32)
    target_link_libraries(snapback PRIVATE user32 gdi32 shell32)
  endif()
  # SNAPBACK_ONNX define/include/lib propagate from snapback_core (PUBLIC); just stage the runtime.
  stage_onnxruntime(snapback)
  set(SNAPBACK_FRONTEND_DIST "${CMAKE_SOURCE_DIR}/frontend/dist" CACHE PATH "Built frontend dist directory")
  if(EXISTS "${SNAPBACK_FRONTEND_DIST}/index.html")
    add_custom_command(TARGET snapback POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${SNAPBACK_FRONTEND_DIST}" "$<TARGET_FILE_DIR:snapback>/frontend")
    install(DIRECTORY "${SNAPBACK_FRONTEND_DIST}/" DESTINATION frontend)
  else()
    message(WARNING "frontend/dist/index.html not found; snapback will fall back to SNAPBACK_FRONTEND_URL or http://localhost:5173")
  endif()
  install(TARGETS snapback RUNTIME DESTINATION .)
  install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION .)

  set(CPACK_PACKAGE_NAME "Snapback")
  set(CPACK_PACKAGE_VENDOR "Snapback")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Snapback local-first focus telemetry demo")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "Snapback")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
  if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "Snapback")
    set(CPACK_NSIS_PACKAGE_NAME "Snapback")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  else()
    set(CPACK_GENERATOR "ZIP")
  endif()
  include(CPack)
endif()
