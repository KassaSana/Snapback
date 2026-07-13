#include "app/frontend_assets.hpp"

#include <array>
#include <cctype>
#include <sstream>

namespace snapback {
namespace {

bool is_unreserved(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' ||
           c == '/' || c == ':';
}

std::string percent_encode_path(std::string value) {
    static constexpr std::array<char, 16> kHex = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

    for (char& c : value) {
        if (c == '\\') c = '/';
    }

    std::string out;
    for (unsigned char c : value) {
        if (is_unreserved(c)) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

}  // namespace

std::string file_url_from_path(const std::filesystem::path& path) {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    return "file:///" + percent_encode_path(absolute.string());
}

std::string resolve_frontend_url(const std::filesystem::path& exe_dir,
                                 const std::optional<std::string>& frontend_url_override) {
    if (frontend_url_override && !frontend_url_override->empty()) return *frontend_url_override;

    const auto bundled_index = exe_dir / "frontend" / "index.html";
    if (std::filesystem::is_regular_file(bundled_index)) {
        return file_url_from_path(bundled_index);
    }

    return "http://localhost:5173";
}

}  // namespace snapback
