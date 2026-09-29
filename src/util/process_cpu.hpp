// CPU this process has used (user + kernel, all threads), the other half of the idle-cost
// figure beside engine_wakeups.
#pragma once

#include <cstdint>

namespace snapback {

// Milliseconds of CPU since process start, or 0 if the platform call fails.
std::uint64_t process_cpu_ms();

}  // namespace snapback
