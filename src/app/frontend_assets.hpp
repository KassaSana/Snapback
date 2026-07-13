#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace snapback {

std::string file_url_from_path(const std::filesystem::path& path);
std::string resolve_frontend_url(const std::filesystem::path& exe_dir,
                                 const std::optional<std::string>& frontend_url_override);

}  // namespace snapback
