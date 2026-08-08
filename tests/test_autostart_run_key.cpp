// Roadmap 11.7. The Windows Run-key round trip, run against a scratch key instead of the
// shared one.
//
// The case this replaces wrote to the real
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run. Two things were wrong with that. It made
// a passing suite depend on ambient machine state — writing that key is a textbook persistence
// technique, so hardened environments refuse it, and ~33% of early Windows CI runs did. And a
// crash between the write and the restore would leave the *test binary* registered to launch
// at every login, which is exactly the accident 3.0 caused twice in two days on macOS and
// Linux.
//
// Everything here happens under HKCU\Software\Snapback\test-<pid>-<n>, which this file
// creates and deletes. It never names the real Run key.
#include "doctest_wrapper.hpp"

#if defined(_WIN32)

#include <atomic>
#include <string>
#include <vector>

#include "app/autostart_run_key.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace snapback;

namespace {

// Deliberately not "Software\Snapback": that name is the app's to claim, and a test must not
// create — or later delete — a key production might one day own. Nothing outside this file
// writes under SnapbackTests.
constexpr wchar_t kScratchRoot[] = L"Software\\SnapbackTests";

bool process_still_running(DWORD pid) {
    // PROCESS_QUERY_LIMITED_INFORMATION is enough to learn STILL_ACTIVE vs exited, and is
    // what a sibling test process is willing to grant us. A missing pid is the stale case
    // 11.10 exists to clean up.
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!handle) return false;
    DWORD status = 0;
    const BOOL ok = GetExitCodeProcess(handle, &status);
    CloseHandle(handle);
    return ok && status == STILL_ACTIVE;
}

bool parse_scratch_leaf_pid(const std::wstring& leaf, DWORD& pid) {
    // Leaves are test-<pid>-<n>. Anything else under the root is not ours to delete.
    if (leaf.rfind(L"test-", 0) != 0) return false;
    const auto rest = leaf.substr(5);
    const auto dash = rest.find(L'-');
    if (dash == std::wstring::npos || dash == 0) return false;
    try {
        const unsigned long parsed = std::stoul(rest.substr(0, dash));
        if (parsed == 0 || parsed > 0xFFFFFFFFul) return false;
        pid = static_cast<DWORD>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

void sweep_stale_scratch_keys() {
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kScratchRoot, 0, KEY_ENUMERATE_SUB_KEYS, &root) !=
        ERROR_SUCCESS) {
        return;  // nothing has ever been created here
    }

    std::vector<std::wstring> stale;
    wchar_t name[256];
    for (DWORD index = 0;; ++index) {
        DWORD name_chars = static_cast<DWORD>(sizeof(name) / sizeof(name[0]));
        const LONG status = RegEnumKeyExW(root, index, name, &name_chars, nullptr, nullptr,
                                          nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) break;
        DWORD pid = 0;
        if (parse_scratch_leaf_pid(name, pid) && !process_still_running(pid)) {
            stale.emplace_back(name);
        }
    }
    RegCloseKey(root);

    // Delete after enumerating: mutating the key while RegEnumKeyEx walks it skips entries.
    for (const auto& leaf : stale) {
        run_key::delete_key(std::wstring(kScratchRoot) + L"\\" + leaf);
    }
}

DWORD unused_pid() {
    // Find a pid OpenProcess cannot see. Start high so we skip System (4) and similar.
    for (DWORD pid = 100000; pid < 100000 + 10000; ++pid) {
        if (!process_still_running(pid)) return pid;
    }
    return 1;  // PID 1 is not a Windows userspace process
}

bool key_exists(const std::wstring& path) {
    HKEY key = nullptr;
    const LONG status =
        RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (status != ERROR_SUCCESS) return false;
    RegCloseKey(key);
    return true;
}

bool create_empty_key(const std::wstring& path) {
    HKEY key = nullptr;
    const LONG status =
        RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) return false;
    RegCloseKey(key);
    return true;
}

// A unique scratch key per fixture. The pid keeps concurrent test processes apart (11.1
// registers each case as its own CTest entry, so several can run at once) and the counter
// keeps cases within one process apart.
struct ScratchKey {
    std::wstring path;

    ScratchKey() {
        // Roadmap 11.10. A crash or REQUIRE abort skips the destructor, so a previous
        // process's test-<pid>-<n> can linger. Sweep only keys whose pid is gone — deleting
        // the SnapbackTests root would race a concurrent case still using it.
        sweep_stale_scratch_keys();
        static std::atomic<int> counter{0};
        path = std::wstring(kScratchRoot) + L"\\test-" +
               std::to_wstring(GetCurrentProcessId()) + L"-" +
               std::to_wstring(counter.fetch_add(1));
    }

    ~ScratchKey() {
        run_key::delete_key(path);
        // Creating the leaf also created this parent, so removing only the leaf would still
        // leave a key behind on the machine — the exact class of residue 11.7 is about.
        // RegDeleteKey refuses a key that still has subkeys, so a concurrent case's scratch
        // key is never destroyed by this: it simply fails and the last one out succeeds.
        run_key::delete_key(kScratchRoot);
    }

    ScratchKey(const ScratchKey&) = delete;
    ScratchKey& operator=(const ScratchKey&) = delete;
};

}  // namespace

TEST_CASE("run_key round-trips an entry through a scratch key") {
    ScratchKey scratch;

    // Absent key reads as "not registered" rather than failing: reads must be total.
    CHECK_FALSE(run_key::entry_present(scratch.path));

    REQUIRE(run_key::install_entry(scratch.path, L"C:\\Program Files\\Snapback\\snapback.exe"));
    CHECK(run_key::entry_present(scratch.path));

    REQUIRE(run_key::remove_entry(scratch.path));
    CHECK_FALSE(run_key::entry_present(scratch.path));
}

TEST_CASE("run_key removal is idempotent") {
    ScratchKey scratch;

    // Removing from a key that does not exist at all.
    CHECK(run_key::remove_entry(scratch.path));

    REQUIRE(run_key::install_entry(scratch.path, L"C:\\snapback.exe"));
    REQUIRE(run_key::remove_entry(scratch.path));
    // Removing an already-absent entry from a key that does exist. Both are success, because
    // "disable autostart" has been achieved either way.
    CHECK(run_key::remove_entry(scratch.path));
    CHECK_FALSE(run_key::entry_present(scratch.path));
}

TEST_CASE("run_key writes the executable path quoted") {
    ScratchKey scratch;
    const std::wstring executable = L"C:\\Program Files\\Snapback\\snapback.exe";
    REQUIRE(run_key::install_entry(scratch.path, executable));

    // Read the raw value back: the quoting is what makes a path with spaces parse as one
    // argument rather than as "C:\Program" plus junk, and nothing else asserts it end to end.
    HKEY key = nullptr;
    REQUIRE(RegOpenKeyExW(HKEY_CURRENT_USER, scratch.path.c_str(), 0, KEY_QUERY_VALUE, &key) ==
            ERROR_SUCCESS);
    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LONG result = RegQueryValueExW(key, run_key::value_name().c_str(), nullptr, &type,
                                         reinterpret_cast<BYTE*>(buffer), &size);
    RegCloseKey(key);

    REQUIRE(result == ERROR_SUCCESS);
    CHECK(type == REG_SZ);
    CHECK(std::wstring(buffer) == L"\"" + executable + L"\"");
}

TEST_CASE("run_key rejects an empty executable instead of writing a bare quote pair") {
    ScratchKey scratch;
    CHECK_FALSE(run_key::install_entry(scratch.path, L""));
    CHECK_FALSE(run_key::entry_present(scratch.path));
}

TEST_CASE("the production key path is the real Run key and is never written by tests") {
    // Pins the one thing the scratch-key tests above deliberately cannot cover: that
    // production still points at the standard location. A typo here would make autostart
    // silently do nothing on users' machines while every test above stayed green.
    CHECK(run_key::user_run_key_path() ==
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    CHECK(run_key::value_name() == L"Snapback");
}

TEST_CASE("scratch fixture sweeps keys left by a dead process") {
    // Roadmap 11.10. Plant a leaf named for a pid that is not running, then construct a
    // fixture. The sweep must remove the orphan and must not remove a leaf named for us —
    // that is the concurrent-case key a root delete would destroy.
    const auto stale_path =
        std::wstring(kScratchRoot) + L"\\test-" + std::to_wstring(unused_pid()) + L"-0";
    const auto live_path = std::wstring(kScratchRoot) + L"\\test-" +
                           std::to_wstring(GetCurrentProcessId()) + L"-9999";
    REQUIRE(create_empty_key(stale_path));
    REQUIRE(create_empty_key(live_path));
    REQUIRE(key_exists(stale_path));
    REQUIRE(key_exists(live_path));

    ScratchKey scratch;
    CHECK_FALSE(key_exists(stale_path));
    CHECK(key_exists(live_path));
    (void)scratch;

    run_key::delete_key(live_path);
}

TEST_CASE("scratch leaf names that are not test-pid-n are left alone") {
    const auto foreign = std::wstring(kScratchRoot) + L"\\keep-this";
    REQUIRE(create_empty_key(foreign));
    ScratchKey scratch;
    CHECK(key_exists(foreign));
    run_key::delete_key(foreign);
}

#endif  // _WIN32
