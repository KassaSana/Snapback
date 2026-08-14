#include "app/window_lifecycle.hpp"

#if !defined(_WIN32) && !defined(__APPLE__)

namespace snapback {

void enable_close_to_tray(void*) {}
void prepare_app_exit(void*) {}
bool is_close_to_tray_enabled(void*) { return false; }

}  // namespace snapback

#endif
