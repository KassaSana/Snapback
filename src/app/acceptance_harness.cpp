#include "app/acceptance_harness.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

namespace snapback {

std::string load_acceptance_script(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("acceptance script could not be opened: " + path.string());
    }
    std::ostringstream contents;
    contents << in.rdbuf();
    if (!in.eof() && in.fail()) {
        throw std::runtime_error("acceptance script could not be read: " + path.string());
    }
    if (contents.str().empty()) {
        throw std::runtime_error("acceptance script is empty: " + path.string());
    }
    return contents.str();
}

void write_acceptance_verdict(const std::filesystem::path& path,
                              const nlohmann::json& verdict) {
    if (!verdict.is_object()) {
        throw std::runtime_error("acceptance verdict must be a JSON object");
    }
    std::filesystem::create_directories(path.parent_path());
    const auto staged = path.string() + ".tmp";
    {
        std::ofstream out(staged, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("acceptance verdict could not be opened: " + staged);
        }
        out << verdict.dump(2) << '\n';
        out.close();
        if (!out) {
            throw std::runtime_error("acceptance verdict could not be written: " + staged);
        }
    }

    // MinGW does not reliably replace an existing destination with filesystem::rename.
    // The smoke removes stale output before launch, but make the publisher deterministic too.
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::rename(staged, path);
}

}  // namespace snapback
