#pragma once

namespace snapback {

#ifdef SNAPBACK_VERSION
inline constexpr char kSnapbackVersion[] = SNAPBACK_VERSION;
#else
// Source-only builds that bypass CMake still have a safe diagnostic value.
inline constexpr char kSnapbackVersion[] = "0.0.0-dev";
#endif

}  // namespace snapback
