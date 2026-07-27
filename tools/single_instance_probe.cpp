#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>

#include "app/single_instance.hpp"

int main(int argc, char** argv) {
    if (argc == 4 && std::string_view(argv[1]) == "hold") {
        const std::filesystem::path ready_path(argv[2]);
        const std::filesystem::path release_path(argv[3]);
        {
            std::ofstream ready(ready_path);
            if (!ready) return 3;
            ready << "ready";
        }
        for (int attempt = 0; attempt < 1000; ++attempt) {
            if (std::filesystem::exists(release_path)) return 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return 4;
    }
    if (argc != 3) return 2;
    const auto guard =
        snapback::SingleInstanceGuard::acquire(std::filesystem::path(argv[1]));
    const std::string_view expected(argv[2]);
    if (expected == "acquired") return guard.acquired() ? 0 : 1;
    if (expected == "blocked") {
        return guard.status() == snapback::SingleInstanceStatus::AlreadyRunning ? 0 : 1;
    }
    return 2;
}
