#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>

#include "app/activation_channel.hpp"
#include "app/single_instance.hpp"

int main(int argc, char** argv) {
    // ROADMAP 9.15. The activation channel is a promise between two *processes*: an in-process
    // listener and client would still pass if the endpoint were somehow process-local, which is
    // exactly the property under test. This mode is the second process.
    if (argc == 4 && std::string_view(argv[1]) == "activate") {
        const auto result =
            snapback::request_activation(std::filesystem::path(argv[2]),
                                         snapback::kActivationTimeoutMs);
        // One word per outcome, rather than reusing activation_result_as_str: that spelling is
        // for a log line a person reads ("no owner"), and a space in it becomes two argv
        // entries under _wspawnl, which quotes nothing. The probe would then take the
        // wrong branch and fail for a reason that has nothing to do with the channel.
        const char* token = "error";
        switch (result) {
            case snapback::ActivationResult::Activated: token = "activated"; break;
            case snapback::ActivationResult::NoOwner: token = "noowner"; break;
            case snapback::ActivationResult::TimedOut: token = "timedout"; break;
            case snapback::ActivationResult::Refused: token = "refused"; break;
            case snapback::ActivationResult::Error: break;
        }
        return std::string_view(argv[3]) == token ? 0 : 1;
    }
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
