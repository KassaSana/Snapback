#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

// A fresh directory under the system temp dir, removed (with anything in it) on destruction.
struct TempDir {
    std::filesystem::path path;

    TempDir() {
        static std::atomic<unsigned> counter{0};
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("snapback_test_" + std::to_string(ticks) + "_" + std::to_string(++counter));
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ignored;
        // A test may have locked the directory down; make sure it can still be removed.
        std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::add, ignored);
        std::filesystem::remove_all(path, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};
