#include "app/single_instance.hpp"

#include <cerrno>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace snapback {

SingleInstanceGuard SingleInstanceGuard::acquire(
    const std::filesystem::path& lock_path) {
#if defined(_WIN32)
    const HANDLE handle =
        CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                    /*dwShareMode=*/0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
            return {SingleInstanceStatus::AlreadyRunning, -1,
                    "another Snapback instance is already running"};
        }
        return {SingleInstanceStatus::Error, -1,
                "could not acquire instance lock (Windows error " +
                    std::to_string(error) + ")"};
    }
    return {SingleInstanceStatus::Acquired,
            reinterpret_cast<std::intptr_t>(handle), ""};
#else
    int open_flags = O_CREAT | O_RDWR;
#if defined(O_CLOEXEC)
    open_flags |= O_CLOEXEC;
#endif
    const int fd = ::open(lock_path.c_str(), open_flags, 0600);
    if (fd < 0) {
        return {SingleInstanceStatus::Error, -1,
                "could not open instance lock: " + std::string(std::strerror(errno))};
    }
#if !defined(O_CLOEXEC)
    const int descriptor_flags = ::fcntl(fd, F_GETFD);
    if (descriptor_flags < 0 ||
        ::fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0) {
        const int error = errno;
        ::close(fd);
        return {SingleInstanceStatus::Error, -1,
                "could not protect instance lock across exec: " +
                    std::string(std::strerror(error))};
    }
#endif
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int error = errno;
        ::close(fd);
        if (error == EWOULDBLOCK || error == EAGAIN) {
            return {SingleInstanceStatus::AlreadyRunning, -1,
                    "another Snapback instance is already running"};
        }
        return {SingleInstanceStatus::Error, -1,
                "could not acquire instance lock: " +
                    std::string(std::strerror(error))};
    }
    return {SingleInstanceStatus::Acquired, static_cast<std::intptr_t>(fd), ""};
#endif
}

SingleInstanceGuard::~SingleInstanceGuard() {
    release();
}

SingleInstanceGuard::SingleInstanceGuard(SingleInstanceGuard&& other) noexcept
    : status_(std::exchange(other.status_, SingleInstanceStatus::Error)),
      native_handle_(std::exchange(other.native_handle_, -1)),
      message_(std::move(other.message_)) {}

SingleInstanceGuard& SingleInstanceGuard::operator=(
    SingleInstanceGuard&& other) noexcept {
    if (this == &other) return *this;
    release();
    status_ = std::exchange(other.status_, SingleInstanceStatus::Error);
    native_handle_ = std::exchange(other.native_handle_, -1);
    message_ = std::move(other.message_);
    return *this;
}

void SingleInstanceGuard::release() noexcept {
    if (native_handle_ == -1) return;
#if defined(_WIN32)
    CloseHandle(reinterpret_cast<HANDLE>(native_handle_));
#else
    ::flock(static_cast<int>(native_handle_), LOCK_UN);
    ::close(static_cast<int>(native_handle_));
#endif
    native_handle_ = -1;
}

}  // namespace snapback
