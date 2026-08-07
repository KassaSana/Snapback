#include "doctest_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "util/private_dir.hpp"

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace snapback;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("snapback_private_dir_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::add, ignored);
        std::filesystem::remove_all(path, ignored);
    }
};

#if !defined(_WIN32)
// The permission bits actually on disk, so an assertion reads as "what the filesystem says"
// rather than "what we asked for".
int mode_of(const std::filesystem::path& path) {
    struct ::stat info {};
    if (::lstat(path.string().c_str(), &info) != 0) return -1;
    return static_cast<int>(info.st_mode & 07777);
}

// Runs `body` with a deliberately permissive umask, restoring the process's own afterwards.
// 022 is the common default and the one that leaves a data directory at 0755.
struct WithUmask {
    ::mode_t previous;
    explicit WithUmask(::mode_t value) : previous(::umask(value)) {}
    ~WithUmask() { ::umask(previous); }
};
#endif

}  // namespace

// --- The permission rules themselves, checkable on any platform ---------------------------

TEST_CASE("the owner-only modes are the ones POSIX actually needs") {
    // 0700 for a directory, not 0600: without the execute bit the owner cannot traverse their
    // own data directory, so an over-tightened directory is as broken as an open one.
    CHECK(private_mode_for(/*is_directory=*/true) == 0700);
    CHECK(private_mode_for(/*is_directory=*/false) == 0600);
}

TEST_CASE("exposure means any bit outside the owner's, including execute") {
    CHECK_FALSE(mode_exposes_others(0700));
    CHECK_FALSE(mode_exposes_others(0600));
    CHECK_FALSE(mode_exposes_others(0400));

    // The default a permissive umask leaves behind, and the reason this item exists.
    CHECK(mode_exposes_others(0755));
    CHECK(mode_exposes_others(0644));

    // Each of the six non-owner bits counts on its own. Execute on a directory is traversal,
    // which is what exposes everything beneath it -- a rule that reads as pedantic until the
    // one bit checked for is read and the directory is 0711.
    CHECK(mode_exposes_others(0740));  // group read
    CHECK(mode_exposes_others(0720));  // group write
    CHECK(mode_exposes_others(0710));  // group execute
    CHECK(mode_exposes_others(0704));  // other read
    CHECK(mode_exposes_others(0702));  // other write
    CHECK(mode_exposes_others(0701));  // other execute

    // Setuid/sticky and the file-type bits live above 0777 and must not be mistaken for
    // exposure; a real st_mode carries them, which is why the mask is 0077 and not ~0700.
    CHECK_FALSE(mode_exposes_others(040700));  // S_IFDIR | 0700
    CHECK_FALSE(mode_exposes_others(0100600)); // S_IFREG | 0600
    CHECK(mode_exposes_others(040755));        // S_IFDIR | 0755
}

// --- The fallback rule, which is testable on every platform -------------------------------

TEST_CASE("no profile directory fails closed instead of choosing a shared path") {
    // Roadmap 8.13. This is the defect with the widest blast radius. Both platforms used to
    // end `return temp_directory_path() / "snapback"` -- the same predictable path for every
    // account, inside a directory every local account can write to. The user got a working app
    // that quietly recorded their window titles somewhere anyone could read, and nothing said
    // so. There is no safe automatic answer, so there is no automatic answer.
    const auto choice = choose_data_dir({}, {});
    CHECK_FALSE(choice.ok);
    CHECK(choice.path.empty());
    // The reason has to be actionable: it names the setting that fixes it.
    CHECK(choice.reason.find("SNAPBACK_DATA_DIR") != std::string::npos);
    // And it must not name a temp directory, which is what it used to silently pick.
    CHECK(choice.reason.find("temp") == std::string::npos);
}

TEST_CASE("an explicit override wins, and a home directory is used when there is one") {
    const std::filesystem::path override_dir = "/somewhere/chosen";
    const std::filesystem::path home = "/home/user/.snapback";

    const auto explicit_choice = choose_data_dir(override_dir, home);
    CHECK(explicit_choice.ok);
    CHECK(explicit_choice.path == override_dir);

    const auto home_choice = choose_data_dir({}, home);
    CHECK(home_choice.ok);
    CHECK(home_choice.path == home);
}

// --- Substitution attacks on a predictable path -------------------------------------------

TEST_CASE("a file where the data directory belongs is refused, not worked around") {
    TempDir temp;
    const auto target = temp.path / "root";
    std::ofstream(target) << "not a directory";

    const auto result = prepare_private_dir(target);
    CHECK_FALSE(result.ok);
    CHECK(result.reason.find("not a directory") != std::string::npos);
}

TEST_CASE("preparing an empty path reports rather than creating something surprising") {
    const auto result = prepare_private_dir({});
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.reason.empty());
}

TEST_CASE("a fresh data directory is created and reports nothing to repair") {
    TempDir temp;
    const auto root = temp.path / "fresh";

    const auto result = prepare_private_dir(root);
    REQUIRE(result.ok);
    CHECK(std::filesystem::is_directory(root));
    CHECK(result.unprotected.empty());
    CHECK(result.fully_private());
}

TEST_CASE("preparing an already-prepared directory is a no-op") {
    // The ordinary path on every launch after the first. It must not report a repair each
    // time, or the log line meant to be noticed becomes noise.
    TempDir temp;
    const auto root = temp.path / "stable";

    REQUIRE(prepare_private_dir(root).ok);
    const auto again = prepare_private_dir(root);
    CHECK(again.ok);
    CHECK(again.repaired.empty());
    CHECK(again.unprotected.empty());
}

#if !defined(_WIN32)

// --- POSIX permissions. These are the substance of 8.13 -----------------------------------

TEST_CASE("a fresh tree is owner-only even under a permissive umask") {
    // The exact condition the item names: umask 022 is the common default, and ordinary
    // `create_directories` under it leaves the data root at 0755 -- readable and traversable
    // by every other local account, along with every window title beneath it.
    TempDir temp;
    WithUmask permissive(022);
    const auto root = temp.path / "umask022";

    const auto result = prepare_private_dir(root);
    REQUIRE(result.ok);
    CHECK(mode_of(root) == 0700);
    CHECK_FALSE(is_group_or_world_accessible(root));

    // Proving the umask really was permissive, so this case cannot pass vacuously on a
    // machine whose umask already happened to be 077.
    const auto control = temp.path / "control";
    std::filesystem::create_directories(control);
    CHECK(mode_of(control) == 0755);
}

TEST_CASE("an upgraded permissive tree is repaired and says what it changed") {
    // An install created by a build from before this existed: the directory and its files
    // carry default modes. The directory mode is what actually protects them, but the files
    // stay individually readable if one is ever copied out, so both are tightened.
    TempDir temp;
    WithUmask permissive(022);
    const auto root = temp.path / "upgraded";
    std::filesystem::create_directories(root / "exports" / "personal");
    std::ofstream(root / "focoflow.db") << "sessions";
    std::ofstream(root / "exports" / "personal" / "snapback_my_data.md") << "window titles";
    REQUIRE(mode_of(root) == 0755);

    const auto result = prepare_private_dir(root);
    REQUIRE(result.ok);
    CHECK(mode_of(root) == 0700);
    CHECK(mode_of(root / "focoflow.db") == 0600);
    CHECK(mode_of(root / "exports" / "personal" / "snapback_my_data.md") == 0600);
    CHECK(mode_of(root / "exports" / "personal") == 0700);
    // Reported, so the user can see that something was wrong rather than only that it is now
    // right. The root plus three entries beneath it.
    CHECK(result.repaired.size() >= 4);
    CHECK(result.unprotected.empty());
    CHECK(result.fully_private());
}

TEST_CASE("a symbolic link where the data directory belongs is refused") {
    // Substitution on a predictable path. Following it would place the user's history wherever
    // the link points, and chmod-ing through it would retarget the permission change at a file
    // outside the data directory entirely.
    TempDir temp;
    const auto elsewhere = temp.path / "attacker";
    std::filesystem::create_directories(elsewhere);
    const auto root = temp.path / "linked";
    std::error_code ec;
    std::filesystem::create_directory_symlink(elsewhere, root, ec);
    if (ec) return;  // some filesystems and CI sandboxes disallow symlinks entirely

    const auto result = prepare_private_dir(root);
    CHECK_FALSE(result.ok);
    CHECK(result.reason.find("symbolic link") != std::string::npos);
}

TEST_CASE("a symlink inside the tree is reported, not followed") {
    TempDir temp;
    const auto root = temp.path / "withlink";
    REQUIRE(prepare_private_dir(root).ok);

    const auto outside = temp.path / "outside.txt";
    std::ofstream(outside) << "someone else's file";
    ::chmod(outside.string().c_str(), 0666);
    std::error_code ec;
    std::filesystem::create_symlink(outside, root / "link.txt", ec);
    if (ec) return;

    const auto result = prepare_private_dir(root);
    CHECK(result.ok);  // the directory itself is fine
    // The link is world-accessible and was left alone rather than chmod-ed through.
    CHECK(result.unprotected.size() == 1);
    CHECK(result.unprotected.front().find("symbolic link") != std::string::npos);
    CHECK_FALSE(result.fully_private());
    // And the target outside the tree was not touched, which is the point.
    CHECK((mode_of(outside) & 0066) != 0);
}

TEST_CASE("make_private reports a failure instead of claiming protection") {
    std::string error;
    CHECK_FALSE(make_private("/definitely/not/a/real/path/anywhere", error));
    CHECK_FALSE(error.empty());
}

#endif  // !_WIN32
