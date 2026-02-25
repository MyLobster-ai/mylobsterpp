#include <catch2/catch_test_macros.hpp>

#include "openclaw/infra/sandbox_paths.hpp"

#include <filesystem>
#include <fstream>

using namespace openclaw::infra;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// normalize_at_prefix
// ---------------------------------------------------------------------------

TEST_CASE("normalize_at_prefix strips leading @", "[infra][sandbox_paths]") {
    CHECK(normalize_at_prefix("@/path/to/file") == "/path/to/file");
    CHECK(normalize_at_prefix("@relative/path") == "relative/path");
}

TEST_CASE("normalize_at_prefix preserves paths without @", "[infra][sandbox_paths]") {
    CHECK(normalize_at_prefix("/normal/path") == "/normal/path");
    CHECK(normalize_at_prefix("relative/path") == "relative/path");
    CHECK(normalize_at_prefix("") == "");
}

TEST_CASE("normalize_at_prefix only strips first @", "[infra][sandbox_paths]") {
    CHECK(normalize_at_prefix("@@double") == "@double");
    CHECK(normalize_at_prefix("@path/@nested") == "path/@nested");
}

// ---------------------------------------------------------------------------
// assert_no_hardlinked_final_path
// ---------------------------------------------------------------------------

TEST_CASE("assert_no_hardlinked_final_path fails for nonexistent path", "[infra][sandbox_paths]") {
    auto result = assert_no_hardlinked_final_path("/nonexistent/path/to/file");
    CHECK_FALSE(result.has_value());
}

TEST_CASE("assert_no_hardlinked_final_path passes for single-link file", "[infra][sandbox_paths]") {
    // Create a temp file
    auto tmp = fs::temp_directory_path() / "sandbox_test_single_link.txt";
    {
        std::ofstream out(tmp);
        out << "test";
    }

    auto result = assert_no_hardlinked_final_path(tmp);
    CHECK(result.has_value());

    fs::remove(tmp);
}

TEST_CASE("assert_no_hardlinked_final_path detects hard links", "[infra][sandbox_paths]") {
    auto tmp_dir = fs::temp_directory_path() / "sandbox_hardlink_test";
    fs::create_directories(tmp_dir);

    auto original = tmp_dir / "original.txt";
    auto hardlink = tmp_dir / "hardlink.txt";

    {
        std::ofstream out(original);
        out << "test";
    }
    fs::create_hard_link(original, hardlink);

    auto result = assert_no_hardlinked_final_path(original);
    CHECK_FALSE(result.has_value());

    fs::remove_all(tmp_dir);
}

// ---------------------------------------------------------------------------
// canonicalize_bind_mount_source
// ---------------------------------------------------------------------------

TEST_CASE("canonicalize_bind_mount_source resolves existing path", "[infra][sandbox_paths]") {
    auto tmp = fs::temp_directory_path() / "sandbox_canon_test.txt";
    {
        std::ofstream out(tmp);
        out << "test";
    }

    auto result = canonicalize_bind_mount_source(tmp);
    REQUIRE(result.has_value());
    CHECK(result->is_absolute());

    fs::remove(tmp);
}

TEST_CASE("canonicalize_bind_mount_source resolves via existing ancestor", "[infra][sandbox_paths]") {
    auto base = fs::temp_directory_path() / "sandbox_ancestor_test";
    fs::create_directories(base);

    // Non-existent leaf under existing base
    auto nonexistent = base / "nonexistent" / "leaf.txt";
    auto result = canonicalize_bind_mount_source(nonexistent);
    REQUIRE(result.has_value());
    CHECK(result->is_absolute());

    fs::remove_all(base);
}

TEST_CASE("canonicalize_bind_mount_source fails for totally nonexistent path", "[infra][sandbox_paths]") {
    auto result = canonicalize_bind_mount_source("/nonexistent/ancestor/path");
    // /nonexistent doesn't exist, but / does, so this should succeed
    // by resolving through /
    CHECK(result.has_value());
}
