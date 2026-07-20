#include "capture/permissions.hpp"

#include <cstdlib>
#include <string>

#if defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace snapback {
namespace {

#if !defined(_WIN32)
bool command_available(const char* command) {
    std::string probe = "command -v ";
    probe += command;
    probe += " >/dev/null 2>/dev/null";
    return std::system(probe.c_str()) == 0;
}
#endif

}  // namespace

PermissionStatus check_capture_permissions(bool capture_running) {
    PermissionStatus status;
    status.capture_probe_confirmed = capture_running;

#if defined(_WIN32)
    status.capture_available = true;
    status.active_window_available = true;
    status.message = "Windows capture backend available.";
#elif defined(__APPLE__)
    const bool trusted = AXIsProcessTrustedWithOptions(nullptr);
    status.capture_available = trusted;
    status.active_window_available = trusted;
    status.message = trusted
                         ? "macOS Accessibility permission is available."
                         : "Grant Accessibility permission to Snapback to read foreground context.";
    if (!trusted) {
        status.setup_steps = {
            "Open System Settings > Privacy & Security > Accessibility.",
            "Enable Snapback or the terminal running Snapback.",
            "Restart Snapback after granting permission.",
        };
    }
#else
    const bool has_display = std::getenv("DISPLAY") != nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr;
    const bool has_xdotool = command_available("xdotool");
    status.capture_available = has_display && has_xdotool;
    status.active_window_available = has_display && has_xdotool;
    status.message = status.capture_available
                         ? "Linux active-window polling backend available."
                         : "Linux capture needs a desktop session and xdotool.";
    if (!has_display) {
        status.setup_steps.push_back("Run Snapback inside an X11/Wayland desktop session.");
    }
    if (!has_xdotool) {
        status.setup_steps.push_back("Install xdotool for active-window polling.");
    }
#endif

    return status;
}

bool request_capture_permissions() {
#if defined(_WIN32)
    // Nothing to request: the low-level hooks need no elevation or user consent.
    return true;
#elif defined(__APPLE__)
    // Passing the prompt option is what makes this differ from the nullptr call in
    // check_capture_permissions above — nullptr checks silently, this one shows the
    // Accessibility dialog when we aren't yet trusted.
    const void* keys[] = {kAXTrustedCheckOptionPrompt};
    const void* values[] = {kCFBooleanTrue};
    CFDictionaryRef options =
        CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
                           &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const bool trusted = AXIsProcessTrustedWithOptions(options);
    if (options) CFRelease(options);
    return trusted;
#else
    // Linux capture needs a desktop session and xdotool, not a permission grant — there is
    // no dialog to raise, so report the current state instead of pretending we asked.
    return check_capture_permissions(false).capture_available;
#endif
}

}  // namespace snapback
