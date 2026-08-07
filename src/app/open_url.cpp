#include "app/open_url.hpp"

#include "app/webview_origin.hpp"

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
#include <shellapi.h>
#elif !defined(__APPLE__)
#include <spawn.h>
#include <sys/wait.h>

#include <vector>

extern char** environ;
#endif

namespace snapback {

bool open_external_url_supported() {
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    return true;
#else
    return false;
#endif
}

namespace detail {

bool open_allowed_external_url(const std::string& url) {
    if (url.empty()) return false;
    return classify_navigation(url, "", false) == NavigationDecision::OpenExternally;
}

#if defined(_WIN32)

bool open_external_url_impl(const std::string& url) {
    const int needed = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (needed <= 0) return false;
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wide.data(), needed) <= 0) return false;
    wide.resize(static_cast<std::size_t>(needed - 1));

    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

#elif !defined(__APPLE__)

bool open_external_url_impl(const std::string& url) {
    const auto argv_owned = external_url_argv(url);
    std::vector<char*> argv;
    argv.reserve(argv_owned.size() + 1);
    for (const auto& arg : argv_owned) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0) return false;

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

#endif

}  // namespace detail

bool open_external_url(const std::string& url) {
    if (!open_external_url_supported()) return false;
    if (!detail::open_allowed_external_url(url)) return false;
    return detail::open_external_url_impl(url);
}

}  // namespace snapback
