// Process-lifetime guard for the shared Snapback data directory.
//
// The lock is owned by an OS handle, not by the presence of the file. A crash closes the
// handle and releases the lock automatically; the harmless file can remain on disk.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace snapback {

enum class SingleInstanceStatus {
    Acquired,
    AlreadyRunning,
    Error,
};

class SingleInstanceGuard {
public:
    static SingleInstanceGuard acquire(const std::filesystem::path& lock_path);

    ~SingleInstanceGuard();
    SingleInstanceGuard(SingleInstanceGuard&& other) noexcept;
    SingleInstanceGuard& operator=(SingleInstanceGuard&& other) noexcept;
    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

    [[nodiscard]] SingleInstanceStatus status() const { return status_; }
    [[nodiscard]] bool acquired() const {
        return status_ == SingleInstanceStatus::Acquired;
    }
    [[nodiscard]] const std::string& message() const { return message_; }

private:
    SingleInstanceGuard(SingleInstanceStatus status, std::intptr_t native_handle,
                        std::string message)
        : status_(status), native_handle_(native_handle), message_(std::move(message)) {}

    void release() noexcept;

    SingleInstanceStatus status_ = SingleInstanceStatus::Error;
    std::intptr_t native_handle_ = -1;
    std::string message_;
};

}  // namespace snapback
