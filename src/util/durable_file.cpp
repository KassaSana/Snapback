#include "util/durable_file.hpp"

#include <system_error>

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
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace snapback {

#if defined(_WIN32)

namespace {

bool flush_handle(HANDLE handle) {
    if (handle == INVALID_HANDLE_VALUE) return false;
    const BOOL ok = FlushFileBuffers(handle);
    CloseHandle(handle);
    return ok != 0;
}

}  // namespace

bool durable_sync_file(const std::filesystem::path& path) {
    // GENERIC_WRITE is required for FlushFileBuffers on some volumes; sharing keeps a
    // concurrent reader (e.g. a load that races a save) from blocking us forever.
    const HANDLE handle =
        CreateFileW(path.native().c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    return flush_handle(handle);
}

bool durable_sync_directory(const std::filesystem::path& dir) {
    // FILE_FLAG_BACKUP_SEMANTICS opens a directory. FlushFileBuffers needs write access —
    // GENERIC_READ alone opens successfully then fails with ERROR_ACCESS_DENIED (5).
    const HANDLE handle =
        CreateFileW(dir.native().c_str(), GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    return flush_handle(handle);
}

#else

bool durable_sync_file(const std::filesystem::path& path) {
    const int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) return false;
    const int result = ::fsync(fd);
    ::close(fd);
    return result == 0;
}

bool durable_sync_directory(const std::filesystem::path& dir) {
    const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    const int result = ::fsync(fd);
    ::close(fd);
    return result == 0;
}

#endif

}  // namespace snapback
