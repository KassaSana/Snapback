#pragma once

#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace snapback {

inline int local_hour_from_rfc3339(const std::string& timestamp) {
    std::tm utc{};
    std::istringstream input(timestamp);
    input >> std::get_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    if (input.fail()) return -1;

#if defined(_WIN32)
    const std::time_t epoch = _mkgmtime(&utc);
#else
    const std::time_t epoch = timegm(&utc);
#endif
    if (epoch == static_cast<std::time_t>(-1)) return -1;

    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &epoch) != 0) return -1;
#else
    if (localtime_r(&epoch, &local) == nullptr) return -1;
#endif
    return local.tm_hour;
}

}  // namespace snapback
