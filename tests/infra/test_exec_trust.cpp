#include <catch2/catch_test_macros.hpp>

#include "openclaw/infra/exec_trust.hpp"

using namespace openclaw::infra;

// ---------------------------------------------------------------------------
// classify_risky_safe_bin_dir
// ---------------------------------------------------------------------------

TEST_CASE("Default trusted dirs are safe", "[infra][exec_trust]") {
    for (const auto& dir : kDefaultTrustedDirs) {
        CHECK_FALSE(classify_risky_safe_bin_dir(dir).has_value());
    }
}

TEST_CASE("Relative paths flagged as risky", "[infra][exec_trust]") {
    auto risk = classify_risky_safe_bin_dir("bin/");
    REQUIRE(risk.has_value());
    CHECK(*risk == SafeBinDirRisk::Relative);
}

TEST_CASE("Empty path flagged as relative", "[infra][exec_trust]") {
    auto risk = classify_risky_safe_bin_dir("");
    REQUIRE(risk.has_value());
    CHECK(*risk == SafeBinDirRisk::Relative);
}

TEST_CASE("Temporary directories flagged", "[infra][exec_trust]") {
    CHECK(classify_risky_safe_bin_dir("/tmp") == SafeBinDirRisk::Temporary);
    CHECK(classify_risky_safe_bin_dir("/tmp/mybin") == SafeBinDirRisk::Temporary);
    CHECK(classify_risky_safe_bin_dir("/var/tmp") == SafeBinDirRisk::Temporary);
    CHECK(classify_risky_safe_bin_dir("/private/tmp") == SafeBinDirRisk::Temporary);
}

TEST_CASE("Package manager directories flagged", "[infra][exec_trust]") {
    CHECK(classify_risky_safe_bin_dir("/usr/local/bin") == SafeBinDirRisk::PackageManager);
    CHECK(classify_risky_safe_bin_dir("/opt/homebrew/bin") == SafeBinDirRisk::PackageManager);
    CHECK(classify_risky_safe_bin_dir("/opt/local/bin") == SafeBinDirRisk::PackageManager);
}

TEST_CASE("Home-scoped directories flagged", "[infra][exec_trust]") {
    CHECK(classify_risky_safe_bin_dir("/home/user/bin") == SafeBinDirRisk::HomeScoped);
    CHECK(classify_risky_safe_bin_dir("/Users/admin/.local/bin") == SafeBinDirRisk::HomeScoped);
}

TEST_CASE("System bin directories are safe", "[infra][exec_trust]") {
    CHECK_FALSE(classify_risky_safe_bin_dir("/bin").has_value());
    CHECK_FALSE(classify_risky_safe_bin_dir("/usr/bin").has_value());
    CHECK_FALSE(classify_risky_safe_bin_dir("/sbin").has_value());
    CHECK_FALSE(classify_risky_safe_bin_dir("/usr/sbin").has_value());
}

// ---------------------------------------------------------------------------
// safe_bin_risk_description
// ---------------------------------------------------------------------------

TEST_CASE("Risk descriptions are non-empty", "[infra][exec_trust]") {
    CHECK_FALSE(safe_bin_risk_description(SafeBinDirRisk::Relative).empty());
    CHECK_FALSE(safe_bin_risk_description(SafeBinDirRisk::Temporary).empty());
    CHECK_FALSE(safe_bin_risk_description(SafeBinDirRisk::PackageManager).empty());
    CHECK_FALSE(safe_bin_risk_description(SafeBinDirRisk::HomeScoped).empty());
}
