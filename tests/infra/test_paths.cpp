#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "openclaw/infra/paths.hpp"

namespace fs = std::filesystem;

TEST_CASE("home_dir returns non-empty path", "[infra][paths]") {
    auto home = openclaw::infra::home_dir();
    CHECK_FALSE(home.empty());
    CHECK(home.is_absolute());
}

TEST_CASE("data_dir returns path containing 'openclaw'", "[infra][paths]") {
    auto dir = openclaw::infra::data_dir();
    CHECK_FALSE(dir.empty());
    CHECK(dir.string().find("openclaw") != std::string::npos);

#ifdef __APPLE__
    SECTION("macOS path contains Library/Application Support") {
        CHECK(dir.string().find("Library/Application Support") != std::string::npos);
    }
#endif
}

TEST_CASE("config_dir returns path containing 'openclaw'", "[infra][paths]") {
    auto dir = openclaw::infra::config_dir();
    CHECK_FALSE(dir.empty());
    CHECK(dir.string().find("openclaw") != std::string::npos);
}

TEST_CASE("cache_dir returns path containing 'openclaw'", "[infra][paths]") {
    auto dir = openclaw::infra::cache_dir();
    CHECK_FALSE(dir.empty());
    CHECK(dir.string().find("openclaw") != std::string::npos);

#ifdef __APPLE__
    SECTION("macOS cache in Library/Caches") {
        CHECK(dir.string().find("Library/Caches") != std::string::npos);
    }
#endif
}

TEST_CASE("logs_dir returns path containing 'openclaw'", "[infra][paths]") {
    auto dir = openclaw::infra::logs_dir();
    CHECK_FALSE(dir.empty());
    CHECK(dir.string().find("openclaw") != std::string::npos);

#ifdef __APPLE__
    SECTION("macOS logs in Library/Logs") {
        CHECK(dir.string().find("Library/Logs") != std::string::npos);
    }
#endif
}

TEST_CASE("runtime_dir returns path containing 'openclaw'", "[infra][paths]") {
    auto dir = openclaw::infra::runtime_dir();
    CHECK_FALSE(dir.empty());
    CHECK(dir.string().find("openclaw") != std::string::npos);
}

TEST_CASE("ensure_dir creates directory", "[infra][paths]") {
    auto tmp = fs::temp_directory_path() / "openclaw_test_ensure" / "nested" / "dir";

    // Clean up if leftover from previous test
    fs::remove_all(fs::temp_directory_path() / "openclaw_test_ensure");

    auto result = openclaw::infra::ensure_dir(tmp);
    CHECK(fs::exists(tmp));
    CHECK(fs::is_directory(tmp));

    // Clean up
    fs::remove_all(fs::temp_directory_path() / "openclaw_test_ensure");
}

TEST_CASE("ensure_dir is idempotent", "[infra][paths]") {
    auto tmp = fs::temp_directory_path() / "openclaw_test_idempotent";

    fs::remove_all(tmp);

    // First call creates it
    openclaw::infra::ensure_dir(tmp);
    REQUIRE(fs::exists(tmp));

    // Second call should not fail
    auto result = openclaw::infra::ensure_dir(tmp);
    CHECK(fs::exists(tmp));

    fs::remove_all(tmp);
}

TEST_CASE("all path functions return absolute paths", "[infra][paths]") {
    CHECK(openclaw::infra::home_dir().is_absolute());
    CHECK(openclaw::infra::data_dir().is_absolute());
    CHECK(openclaw::infra::config_dir().is_absolute());
    CHECK(openclaw::infra::cache_dir().is_absolute());
    CHECK(openclaw::infra::logs_dir().is_absolute());
    CHECK(openclaw::infra::runtime_dir().is_absolute());
}
