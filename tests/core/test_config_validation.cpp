#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include "openclaw/core/config.hpp"

using namespace openclaw;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// validate_gateway_auth_mode (v2026.3.7)
// ---------------------------------------------------------------------------

TEST_CASE("validate_gateway_auth_mode: no auth config is valid", "[config][validation]") {
    GatewayConfig config;
    // No auth at all — always valid
    CHECK(validate_gateway_auth_mode(config));
}

TEST_CASE("validate_gateway_auth_mode: token-only is valid without mode", "[config][validation]") {
    GatewayConfig config;
    AuthConfig auth;
    auth.token = "my-secret-token";
    config.auth = auth;
    CHECK(validate_gateway_auth_mode(config));
}

TEST_CASE("validate_gateway_auth_mode: password-only is valid without mode", "[config][validation]") {
    GatewayConfig config;
    AuthConfig auth;
    auth.tailscale_authkey = "tskey-auth-xxx";
    config.auth = auth;
    CHECK(validate_gateway_auth_mode(config));
}

TEST_CASE("validate_gateway_auth_mode: both credentials without mode is INVALID", "[config][validation]") {
    GatewayConfig config;
    AuthConfig auth;
    auth.token = "my-secret-token";
    auth.tailscale_authkey = "tskey-auth-xxx";
    config.auth = auth;
    // No auth_mode set — BREAKING: must be rejected
    CHECK_FALSE(validate_gateway_auth_mode(config));
}

TEST_CASE("validate_gateway_auth_mode: both credentials WITH mode is valid", "[config][validation]") {
    GatewayConfig config;
    AuthConfig auth;
    auth.token = "my-secret-token";
    auth.tailscale_authkey = "tskey-auth-xxx";
    config.auth = auth;
    config.auth_mode = "token";
    CHECK(validate_gateway_auth_mode(config));
}

TEST_CASE("validate_gateway_auth_mode: empty strings don't count as set", "[config][validation]") {
    GatewayConfig config;
    AuthConfig auth;
    auth.token = "";
    auth.tailscale_authkey = "tskey-auth-xxx";
    config.auth = auth;
    // Empty token doesn't count — only password set, so no mode needed
    CHECK(validate_gateway_auth_mode(config));
}

// ---------------------------------------------------------------------------
// validate_brave_language_code (v2026.3.8)
// ---------------------------------------------------------------------------

TEST_CASE("validate_brave_language_code: standard 2-letter codes", "[config][validation]") {
    CHECK(validate_brave_language_code("en"));
    CHECK(validate_brave_language_code("de"));
    CHECK(validate_brave_language_code("fr"));
    CHECK(validate_brave_language_code("ja"));
    CHECK(validate_brave_language_code("ko"));
    CHECK(validate_brave_language_code("zh"));
    CHECK(validate_brave_language_code("ar"));
    CHECK(validate_brave_language_code("ru"));
    CHECK(validate_brave_language_code("es"));
    CHECK(validate_brave_language_code("pt"));
}

TEST_CASE("validate_brave_language_code: extended codes", "[config][validation]") {
    CHECK(validate_brave_language_code("zh-hans"));
    CHECK(validate_brave_language_code("zh-hant"));
    CHECK(validate_brave_language_code("en-gb"));
    CHECK(validate_brave_language_code("pt-br"));
}

TEST_CASE("validate_brave_language_code: invalid codes rejected", "[config][validation]") {
    CHECK_FALSE(validate_brave_language_code(""));
    CHECK_FALSE(validate_brave_language_code("xx"));
    CHECK_FALSE(validate_brave_language_code("english"));
    CHECK_FALSE(validate_brave_language_code("en-us"));  // Not in the supported list
    CHECK_FALSE(validate_brave_language_code("ZH"));     // Case-sensitive
}

// ---------------------------------------------------------------------------
// Config fail-closed on invalid JSON (v2026.3.7)
// ---------------------------------------------------------------------------

TEST_CASE("load_config throws on invalid JSON (fail-closed)", "[config][validation]") {
    // Create a temp file with invalid JSON
    auto tmp_path = std::filesystem::temp_directory_path() / "test_invalid_config.json";
    {
        std::ofstream f(tmp_path);
        f << "{ this is not valid json !!!";
    }

    CHECK_THROWS_AS(load_config(tmp_path), std::runtime_error);

    std::filesystem::remove(tmp_path);
}

TEST_CASE("load_config succeeds on valid minimal JSON", "[config][validation]") {
    auto tmp_path = std::filesystem::temp_directory_path() / "test_valid_config.json";
    {
        std::ofstream f(tmp_path);
        f << "{}";
    }

    auto config = load_config(tmp_path);
    // Should return valid defaults
    CHECK(config.gateway.port == 18789);
    CHECK(config.log_level == "info");

    std::filesystem::remove(tmp_path);
}

// ---------------------------------------------------------------------------
// GatewayConfig.auth_mode JSON round-trip
// ---------------------------------------------------------------------------

TEST_CASE("GatewayConfig auth_mode JSON round-trip", "[config]") {
    GatewayConfig config;
    config.auth_mode = "token";

    json j = config;
    auto decoded = j.get<GatewayConfig>();
    CHECK(decoded.auth_mode.has_value());
    CHECK(*decoded.auth_mode == "token");
}

TEST_CASE("GatewayConfig auth_mode absent in JSON", "[config]") {
    GatewayConfig config;
    json j = config;
    auto decoded = j.get<GatewayConfig>();
    CHECK_FALSE(decoded.auth_mode.has_value());
}

// ---------------------------------------------------------------------------
// ContextEngineConfig JSON round-trip
// ---------------------------------------------------------------------------

TEST_CASE("ContextEngineConfig JSON round-trip", "[config]") {
    ContextEngineConfig config;
    config.plugin_id = "lossless-claw";
    config.plugin_config = {{"max_tokens", 100000}};

    json j = config;
    auto decoded = j.get<ContextEngineConfig>();
    CHECK(decoded.plugin_id == "lossless-claw");
    CHECK(decoded.plugin_config["max_tokens"] == 100000);
}

TEST_CASE("ContextEngineConfig defaults to no plugin", "[config]") {
    ContextEngineConfig config;
    CHECK_FALSE(config.plugin_id.has_value());
}

// ---------------------------------------------------------------------------
// SessionConfig with CompactionConfig
// ---------------------------------------------------------------------------

TEST_CASE("SessionConfig compaction JSON round-trip", "[config]") {
    SessionConfig config;
    config.compaction = CompactionConfig{};
    config.compaction->model = "claude-haiku-4-5";
    config.compaction->quality_audit_enabled = false;

    json j = config;
    auto decoded = j.get<SessionConfig>();
    REQUIRE(decoded.compaction.has_value());
    CHECK(decoded.compaction->model == "claude-haiku-4-5");
    CHECK_FALSE(decoded.compaction->quality_audit_enabled);
}

// ---------------------------------------------------------------------------
// Full Config with new v2026.3.11 sections
// ---------------------------------------------------------------------------

TEST_CASE("Full Config includes v2026.3.11 sections", "[config]") {
    Config config;
    config.web_search = WebSearchConfig{};
    config.web_search->provider = "brave";
    config.talk = TalkConfig{};
    config.talk->silence_timeout_ms = 3000;
    config.audio = AudioConfig{};
    config.cli_banner = CliBannerConfig{};
    config.context_engine = ContextEngineConfig{};

    json j = config;
    auto decoded = j.get<Config>();
    REQUIRE(decoded.web_search.has_value());
    CHECK(decoded.web_search->provider == "brave");
    REQUIRE(decoded.talk.has_value());
    CHECK(decoded.talk->silence_timeout_ms == 3000);
    REQUIRE(decoded.audio.has_value());
    REQUIRE(decoded.cli_banner.has_value());
    REQUIRE(decoded.context_engine.has_value());
}

// ---------------------------------------------------------------------------
// MemoryConfig extra_paths
// ---------------------------------------------------------------------------

TEST_CASE("MemoryConfig extra_paths JSON round-trip", "[config]") {
    MemoryConfig config;
    config.extra_paths = {{"/home/user/docs", "/home/user/code"}};

    json j = config;
    auto decoded = j.get<MemoryConfig>();
    REQUIRE(decoded.extra_paths.has_value());
    CHECK(decoded.extra_paths->size() == 2);
    CHECK((*decoded.extra_paths)[0] == "/home/user/docs");
}

// ---------------------------------------------------------------------------
// expand_tilde
// ---------------------------------------------------------------------------

TEST_CASE("expand_tilde expands home directory", "[config]") {
    auto result = expand_tilde("~/Documents");
    CHECK_FALSE(result.empty());
    CHECK_FALSE(result.starts_with("~"));
    CHECK(result.ends_with("/Documents"));
}

TEST_CASE("expand_tilde passes through non-tilde paths", "[config]") {
    CHECK(expand_tilde("/absolute/path") == "/absolute/path");
    CHECK(expand_tilde("relative/path") == "relative/path");
    CHECK(expand_tilde("") == "");
}
