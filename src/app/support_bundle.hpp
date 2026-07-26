// Privacy-scoped diagnostics export for user-initiated support requests.
#pragma once

#include <filesystem>
#include <string>

#include "types.hpp"

namespace snapback {

inline constexpr const char* kSupportBundlePrivacyNotice =
    "Contains the app version, OS/build details, current health, and recent Snapback log "
    "lines. It does not contain the database, session history, window titles, captured "
    "input, or training exports. Health details and log lines may contain local file paths "
    "and error messages; review the file before sharing.";

struct SupportBundleExportResult {
    std::string output_path;
    std::string privacy_notice;
};

SupportBundleExportResult export_support_bundle(
    const std::filesystem::path& output_dir,
    const DiagnosticsSnapshot& diagnostics);

}  // namespace snapback
