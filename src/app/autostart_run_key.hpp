// The Windows Run-key mechanism, with the key path as an argument so tests round-trip a scratch
// key under HKCU instead of the real Run key. Windows-only.
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
