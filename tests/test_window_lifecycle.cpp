#include "app/window_lifecycle.hpp"

#include <doctest/doctest.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#endif

TEST_CASE("window_lifecycle: null handle safety") {
    using namespace snapback;

    CHECK_FALSE(is_close_to_tray_enabled(nullptr));
    enable_close_to_tray(nullptr);
    CHECK_FALSE(is_close_to_tray_enabled(nullptr));
    prepare_app_exit(nullptr);
    CHECK_FALSE(is_close_to_tray_enabled(nullptr));
}

#if defined(_WIN32)
TEST_CASE("window_lifecycle: Win32 close-to-tray intercepts WM_CLOSE and hides window") {
    using namespace snapback;

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.lpszClassName = L"SnapbackTestLifecycleWindow";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"SnapbackTestLifecycleWindow", L"Test Window",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                400, 300, nullptr, nullptr, inst, nullptr);
    REQUIRE(hwnd != nullptr);

    ShowWindow(hwnd, SW_SHOW);
    CHECK(IsWindowVisible(hwnd));
    CHECK_FALSE(is_close_to_tray_enabled(hwnd));

    // Enable close to tray
    enable_close_to_tray(hwnd);
    CHECK(is_close_to_tray_enabled(hwnd));

    // Send WM_CLOSE — should hide window instead of destroying it
    SendMessageW(hwnd, WM_CLOSE, 0, 0);
    CHECK(IsWindow(hwnd));               // Window was NOT destroyed
    CHECK_FALSE(IsWindowVisible(hwnd));  // Window was hidden

    // Show window again
    ShowWindow(hwnd, SW_SHOW);
    CHECK(IsWindowVisible(hwnd));

    // Prepare app exit — removes subclass
    prepare_app_exit(hwnd);
    CHECK_FALSE(is_close_to_tray_enabled(hwnd));

    DestroyWindow(hwnd);
}
#endif
