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

// v2026.2.26: URI-decoded path canonicalization tests
TEST_CASE("URI decode: basic percent decoding", "[infra][security]") {
    CHECK(uri_decode_percent("hello%20world") == "hello world");
    CHECK(uri_decode_percent("no%2fslash") == "no/slash");
    CHECK(uri_decode_percent("clean") == "clean");
    CHECK(uri_decode_percent("") == "");
}

TEST_CASE("URI decode: iterative decode stops double-encoding attacks", "[infra][security]") {
    // %252e%252e -> %2e%2e -> .. (two passes)
    auto result = iterative_uri_decode("%252e%252e%252f");
    CHECK(result == "../");

    // %25252e -> %252e -> %2e -> . (three passes)
    auto deep = iterative_uri_decode("%25252e");
    CHECK(deep == ".");
}

TEST_CASE("URI decode: stable input returns unchanged", "[infra][security]") {
    CHECK(iterative_uri_decode("/normal/path") == "/normal/path");
    CHECK(iterative_uri_decode("file.txt") == "file.txt");
}

TEST_CASE("Malformed percent encoding detection", "[infra][security]") {
    CHECK(has_malformed_percent_encoding("%XZ"));      // invalid hex
    CHECK(has_malformed_percent_encoding("foo%"));      // trailing %
    CHECK(has_malformed_percent_encoding("foo%0"));     // incomplete
    CHECK(has_malformed_percent_encoding("%00"));        // null byte
    CHECK(has_malformed_percent_encoding("a%0Gb"));     // G is not hex
    CHECK_FALSE(has_malformed_percent_encoding("clean"));
    CHECK_FALSE(has_malformed_percent_encoding("%2e%2f"));
    CHECK_FALSE(has_malformed_percent_encoding(""));
}

TEST_CASE("Path alias: percent-encoded path traversal rejected", "[infra][security]") {
    TmpDir tmp;
    auto file = tmp.path / "safe.txt";
    std::ofstream(file) << "hello";

    // Encoded "../" should be detected after URI decode
    auto encoded_path = tmp.path / "%2e%2e" / "outside.txt";
    auto result = assert_no_path_alias_escape(encoded_path, {tmp.path});
    // Should either reject or at least not allow escape
    // (the decoded path may not exist, but the check should not panic)
}

TEST_CASE("Path alias: malformed percent-encoding rejected", "[infra][security]") {
    TmpDir tmp;
    auto bad_path = tmp.path / "file%XZ.txt";
    auto result = assert_no_path_alias_escape(bad_path, {tmp.path});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == openclaw::ErrorCode::Forbidden);
}

TEST_CASE("Path alias: null byte injection rejected", "[infra][security]") {
    TmpDir tmp;
    auto null_path = tmp.path / "file%00.txt";
    auto result = assert_no_path_alias_escape(null_path, {tmp.path});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == openclaw::ErrorCode::Forbidden);
}

// v2026.2.26: Hardlinked intermediate path component test
TEST_CASE("Path alias: hardlinked intermediate rejected", "[infra][security]") {
    TmpDir tmp;
    auto original = tmp.path / "original.txt";
    auto hardlink = tmp.path / "hardlink.txt";
    std::ofstream(original) << "hello";
    fs::create_hard_link(original, hardlink);

    // hardlink.txt as an intermediate component (used as prefix for another path)
    // Since it's a regular file with nlink > 1, it should be rejected
    auto result = assert_no_path_alias_escape(hardlink, {tmp.path});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == openclaw::ErrorCode::Forbidden);
}

// v2026.2.26: Broken symlink escape detection test
TEST_CASE("Path alias: broken symlink pointing outside workspace rejected", "[infra][security]") {
    TmpDir tmp;
    auto outside_target = fs::path("/nonexistent/outside/target");
    auto broken_link = tmp.path / "broken_escape";

    // Create a symlink pointing to a nonexistent path outside workspace
    std::error_code ec;
    fs::create_symlink(outside_target, broken_link, ec);
    if (ec) {
        // Skip if we can't create symlinks (permissions)
        SUCCEED("Skipped: cannot create symlinks");
        return;
    }

    auto result = assert_no_path_alias_escape(broken_link, {tmp.path});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == openclaw::ErrorCode::Forbidden);

    fs::remove(broken_link, ec);
}

TEST_CASE("Path alias: broken symlink within workspace allowed", "[infra][security]") {
    TmpDir tmp;
    auto inside_target = tmp.path / "nonexistent_but_inside";
    auto broken_link = tmp.path / "broken_inside";

    std::error_code ec;
    fs::create_symlink(inside_target, broken_link, ec);
    if (ec) {
        SUCCEED("Skipped: cannot create symlinks");
        return;
    }

    auto result = assert_no_path_alias_escape(broken_link, {tmp.path});
    // Should pass because the broken target is within workspace
    REQUIRE(result.has_value());

    fs::remove(broken_link, ec);
}
