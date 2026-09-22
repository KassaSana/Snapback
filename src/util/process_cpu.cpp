#include "util/process_cpu.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/resource.h>
#include <sys/time.h>
#endif

namespace snapback {

#if defined(_WIN32)

std::uint64_t process_cpu_ms() {
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return 0;
    }
    // FILETIME counts 100-nanosecond intervals across two 32-bit halves. ULARGE_INTEGER is
    // the documented way to rejoin them; casting the struct to a uint64 would be an
    // alignment bug waiting for a compiler that cares.
    const auto to_ms = [](const FILETIME& value) -> std::uint64_t {
        ULARGE_INTEGER joined;
        joined.LowPart = value.dwLowDateTime;
        joined.HighPart = value.dwHighDateTime;
        return joined.QuadPart / 10000u;
    };
    return to_ms(kernel) + to_ms(user);
}

#else

std::uint64_t process_cpu_ms() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    const auto to_ms = [](const timeval& value) -> std::uint64_t {
        return static_cast<std::uint64_t>(value.tv_sec) * 1000u +
               static_cast<std::uint64_t>(value.tv_usec) / 1000u;
    };
    return to_ms(usage.ru_utime) + to_ms(usage.ru_stime);
}

#endif

}  // namespace snapback
