// Objective-C++ window shims declared in plain C++, so main.cpp stays a .cpp.
#pragma once

#if defined(__APPLE__)

namespace snapback {
namespace mac {

// Bring the main window forward and activate the app (ordering the window front alone leaves it
// behind the frontmost app). A null `ns_window` only activates.
void bring_window_to_front(void* ns_window);

}  // namespace mac
}  // namespace snapback

#endif  // __APPLE__
