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

// A unique scratch key per fixture. The pid keeps concurrent test processes apart (11.1
// registers each case as its own CTest entry, so several can run at once) and the counter
// keeps cases within one process apart.
struct ScratchKey {
    std::wstring path;

    ScratchKey() {
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

#endif  // _WIN32
