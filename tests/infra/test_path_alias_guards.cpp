#include <catch2/catch_test_macros.hpp>

#include "openclaw/infra/path_alias_guards.hpp"

#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace openclaw::infra;

namespace {
struct TmpDir {
    fs::path path;
    TmpDir() {
        path = fs::temp_directory_path() / ("test_path_alias_" + std::to_string(::getpid()));
        fs::create_directories(path);
    }
    ~TmpDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};
} // namespace

TEST_CASE("Path alias guards: normal file passes", "[infra][security]") {
    TmpDir tmp;
    auto file = tmp.path / "normal.txt";
    std::ofstream(file) << "hello";

    auto result = assert_no_path_alias_escape(file, {tmp.path});
    REQUIRE(result.has_value());
}

TEST_CASE("Path alias guards: symlink within workspace passes", "[infra][security]") {
    TmpDir tmp;
    auto target = tmp.path / "target.txt";
    auto link = tmp.path / "link.txt";
    std::ofstream(target) << "hello";
    fs::create_symlink(target, link);

    auto result = assert_no_path_alias_escape(link, {tmp.path});
    REQUIRE(result.has_value());
}

TEST_CASE("Path alias guards: symlink escaping workspace rejected", "[infra][security]") {
    TmpDir tmp;
    auto outside = fs::temp_directory_path() / "outside_workspace.txt";
    std::ofstream(outside) << "secret";
    auto link = tmp.path / "escape_link";
    fs::create_symlink(outside, link);

    auto result = assert_no_path_alias_escape(link, {tmp.path});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == openclaw::ErrorCode::Forbidden);

    // Cleanup
    std::error_code ec;
    fs::remove(outside, ec);
}

TEST_CASE("Path alias guards: empty workspace roots rejected", "[infra][security]") {
    TmpDir tmp;
    auto file = tmp.path / "file.txt";
    std::ofstream(file) << "hello";

    auto result = assert_no_path_alias_escape(file, {});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == openclaw::ErrorCode::InvalidArgument);
}

TEST_CASE("Hardlink detection: single-link file passes", "[infra][security]") {
    TmpDir tmp;
    auto file = tmp.path / "single.txt";
    std::ofstream(file) << "hello";

    auto result = assert_no_hardlinked_final_path_strict(file);
    REQUIRE(result.has_value());
}

TEST_CASE("Hardlink detection: hardlinked file rejected", "[infra][security]") {
    TmpDir tmp;
    auto original = tmp.path / "original.txt";
    auto hardlink = tmp.path / "hardlink.txt";
    std::ofstream(original) << "hello";
    fs::create_hard_link(original, hardlink);

    auto result = assert_no_hardlinked_final_path_strict(original);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == openclaw::ErrorCode::Forbidden);
}

TEST_CASE("Hardlink detection: UnlinkTarget policy removes link", "[infra][security]") {
    TmpDir tmp;
    auto original = tmp.path / "orig_unlink.txt";
    auto hardlink = tmp.path / "hard_unlink.txt";
    std::ofstream(original) << "hello";
    fs::create_hard_link(original, hardlink);

    // UnlinkTarget should remove the hardlink path
    auto result = assert_no_hardlinked_final_path_strict(
        hardlink, PathAliasPolicy::UnlinkTarget);
    REQUIRE(result.has_value());
    CHECK_FALSE(fs::exists(hardlink));
    CHECK(fs::exists(original));  // Original should still exist
}

TEST_CASE("Combined path safety check", "[infra][security]") {
    TmpDir tmp;
    auto file = tmp.path / "safe.txt";
    std::ofstream(file) << "hello";

    auto result = assert_path_safe(file, {tmp.path});
    REQUIRE(result.has_value());
}
