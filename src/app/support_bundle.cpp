#include "app/support_bundle.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "util/logger.hpp"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <sys/utsname.h>
#endif

namespace snapback {
namespace {

struct OsIdentity {
    std::string family;
    std::string release;
    std::string build;
};

OsIdentity os_identity() {
#if defined(_WIN32)
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtl_get_version =
        ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))
              : nullptr;
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (rtl_get_version && rtl_get_version(&version) == 0) {
        return {"windows",
                std::to_string(version.dwMajorVersion) + "." +
                    std::to_string(version.dwMinorVersion),
                std::to_string(version.dwBuildNumber)};
    }
    return {"windows", "unknown", "unknown"};
#elif defined(__APPLE__)
    utsname identity{};
    if (uname(&identity) == 0) {
        return {"macos", identity.release, identity.version};
    }
    return {"macos", "unknown", "unknown"};
#elif defined(__linux__)
    utsname identity{};
    if (uname(&identity) == 0) {
        return {"linux", identity.release, identity.version};
    }
    return {"linux", "unknown", "unknown"};
#else
    return {"unknown", "unknown", "unknown"};
#endif
}

const char* architecture_name() {
#if defined(_M_X64) || defined(__x86_64__)
    return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

std::string filename_timestamp() {
    std::string timestamp = utc_timestamp();
    std::replace(timestamp.begin(), timestamp.end(), ':', '-');
    return timestamp;
}

}  // namespace

SupportBundleExportResult export_support_bundle(
    const std::filesystem::path& output_dir,
    const DiagnosticsSnapshot& diagnostics) {
    std::filesystem::create_directories(output_dir);
    const auto output_path =
        output_dir / ("snapback-support-" + filename_timestamp() + ".json");
    const auto os = os_identity();

    const nlohmann::json bundle{
        {"privacyNotice", kSupportBundlePrivacyNotice},
        {"generatedAt", utc_timestamp()},
        {"version", diagnostics.version},
        {"build",
         {{"platform", os.family},
          {"osRelease", os.release},
          {"osBuild", os.build},
          {"architecture", architecture_name()},
#if defined(NDEBUG)
          {"configuration", "release"}
#else
          {"configuration", "debug"}
#endif
         }},
        {"health", diagnostics.health},
        {"recentLogs", diagnostics.recent_logs},
    };

    std::ofstream out(output_path, std::ios::binary);
    if (!out) throw std::runtime_error("failed to open support bundle");
    out << bundle.dump(2) << '\n';
    out.flush();
    if (!out) throw std::runtime_error("failed to write support bundle");

    return {output_path.string(), kSupportBundlePrivacyNotice};
}

}  // namespace snapback
