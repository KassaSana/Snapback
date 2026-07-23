cmake_minimum_required(VERSION 3.20)
project(snapback_cpp VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest
  GIT_TAG        v2.4.11
  GIT_SHALLOW    TRUE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json doctest)

# The full desktop app (webview + WebView2 + sqlite) is heavy and OS-bound. Keep it
# OFF during the early, pure-logic phases so the test suite builds without it. We
# flip this ON in Phase 6 when the webview IPC bridge lands.
option(SNAPBACK_BUILD_APP "Build the desktop app target (webview + sqlite)" OFF)
option(SNAPBACK_ONNX      "Build with ONNX Runtime inference" OFF)
option(SNAPBACK_BUILD_BENCHMARKS "Build standalone performance benchmarks" OFF)

# SQLite amalgamation (replaces rusqlite). Prefer a local vendored copy when present
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
)
target_include_directories(snapback_core PUBLIC src)
target_link_libraries(snapback_core PUBLIC nlohmann_json::nlohmann_json sqlite3)

# --- ONNX Runtime (optional backend) ---
# The define is PUBLIC on the core so classifier.cpp AND every consumer (tests + app) see
# the same OnnxModel layout. onnxruntime.lib is an import lib; the matching DLL is copied
# next to each exe by copy_onnxruntime_dll() below.
if(SNAPBACK_ONNX)
  set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime" CACHE PATH "ONNX Runtime root")
  target_compile_definitions(snapback_core PUBLIC SNAPBACK_ONNX=1)
  target_include_directories(snapback_core PUBLIC ${ONNXRUNTIME_ROOT}/include)
  target_link_libraries(snapback_core PUBLIC ${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib)

  # Stage onnxruntime.dll next to a target's exe so it's found at runtime.
  function(copy_onnxruntime_dll target)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:${target}>")
  endfunction()
else()
  function(copy_onnxruntime_dll target)
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
  src/app/autostart.cpp
  src/app/frontend_assets.cpp
  src/app/settings.cpp
  src/app/state.cpp
  src/app/training_deploy.cpp
)
target_include_directories(snapback_app PUBLIC src)
target_link_libraries(snapback_app PUBLIC snapback_core snapback_capture)
target_compile_definitions(snapback_app PUBLIC SNAPBACK_VERSION=\"${PROJECT_VERSION}\")

# --- tests (doctest replaces `cargo test`) ---
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
copy_onnxruntime_dll(snapback_tests)
add_test(NAME snapback_tests COMMAND snapback_tests)

add_executable(snapback_feature_parity_export tools/feature_parity_export.cpp)
target_link_libraries(snapback_feature_parity_export PRIVATE snapback_core)

if(SNAPBACK_BUILD_BENCHMARKS)
  add_executable(snapback_benchmarks benchmarks/bench_snapback.cpp)
  target_include_directories(snapback_benchmarks PRIVATE benchmarks)
  # snapback_app already PUBLIC-links snapback_core + snapback_capture transitively.
  target_link_libraries(snapback_benchmarks PRIVATE snapback_app)

  # Targeted hot-path micro-benchmarks (producer/consumer/lock/SQLite).
  add_executable(snapback_hotpath_benchmarks benchmarks/bench_hotpaths.cpp)
  target_include_directories(snapback_hotpath_benchmarks PRIVATE benchmarks)
  target_link_libraries(snapback_hotpath_benchmarks PRIVATE snapback_app)
endif()

# --- the desktop app (later phases) ---
if(SNAPBACK_BUILD_APP)
  FetchContent_Declare(webview
    GIT_REPOSITORY https://github.com/webview/webview
    GIT_TAG        0.12.0)
  FetchContent_MakeAvailable(webview)

  add_executable(snapback
    src/main.cpp
  )
  if(WIN32)
    target_sources(snapback PRIVATE src/snapback/overlay_windows.cpp src/app/tray_windows.cpp)
  else()
    # Overlay::instance()/Tray::instance() live ONLY in the *_windows.cpp files above, so
    # without these no-op fallbacks the app fails to link off Windows (undefined symbols).
    # Real implementations: ROADMAP 3.1 (macOS) / 3.2 (Linux).
    target_sources(snapback PRIVATE src/snapback/overlay_stub.cpp src/app/tray_stub.cpp)
  endif()
  # snapback_app already PUBLIC-links snapback_core (which itself PUBLIC-links sqlite3)
  # and snapback_capture transitively.
  target_link_libraries(snapback PRIVATE snapback_app webview::core)
  if(WIN32)
    target_link_libraries(snapback PRIVATE user32 gdi32 shell32)
  endif()
  # SNAPBACK_ONNX define/include/lib propagate from snapback_core (PUBLIC); just stage the DLL.
  copy_onnxruntime_dll(snapback)
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

  set(CPACK_PACKAGE_NAME "Snapback")
  set(CPACK_PACKAGE_VENDOR "Snapback")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Snapback local-first focus telemetry demo")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "Snapback")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/README.md")
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
