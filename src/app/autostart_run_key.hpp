// The Windows Run-key mechanism, with the key path as an argument. Roadmap 11.7.
//
// This exists for the same reason autostart_launchd.hpp and autostart_systemd.hpp do: the
// mechanism has to be testable without touching the developer's (or the CI runner's) real
// login configuration. Those two take their target *directory* as a parameter; this takes
// the target *key path*, so a test can round-trip against a throwaway key under HKCU and
// delete it afterwards.
//
// Before this, `tests/test_autostart.cpp` wrote to the real
// `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. That made a passing suite depend on
// ambient machine state — writing that key is a textbook persistence technique, so hardened
// environments refuse it, and 2 of the first 6 observed Windows CI runs did exactly that
// (~33%, alternating between the two Windows jobs on identical code). It also meant a test
// crash between the write and the restore would leave the *test binary* registered to launch
// at every login, which is the same accident 3.0 caused twice on macOS and Linux.
//
// Unlike the launchd and systemd modules, this one cannot compile off Windows: there is no
// registry to call. It is guarded, and so is its test.
#pragma once

#if defined(_WIN32)

#include <string>

namespace snapback::run_key {

// The real per-user Run key, relative to HKEY_CURRENT_USER. Production passes this; tests
// pass a scratch path instead.
std::wstring user_run_key_path();

// The value name Snapback registers under, in whichever key it is given.
std::wstring value_name();

// True if a Snapback entry exists under `key_path`. False when the key is absent, which is
// the normal answer rather than an error.
bool entry_present(const std::wstring& key_path);

// Writes the quoted command line for `executable` under `key_path`, creating the key if it
// does not exist. False on any failure, including a refused write.
bool install_entry(const std::wstring& key_path, const std::wstring& executable);

// Removes the Snapback entry from `key_path`. Removing an absent entry is success, so the
// operation is idempotent.
bool remove_entry(const std::wstring& key_path);

// Deletes `key_path` itself. Only used to clean up scratch keys created by tests; production
// never removes the shared Run key.
bool delete_key(const std::wstring& key_path);

// The running executable's full path, or empty on failure.
std::wstring current_executable_path();

}  // namespace snapback::run_key

#endif  // _WIN32
