#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Reproduce the redaction logic for testing
namespace {
void redact_config_json(json& j) {
    static const std::vector<std::string> sensitive_keys = {
        "api_key", "bot_token", "access_token", "token", "secret",
        "signing_secret", "app_token", "verify_token",
    };
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            bool is_sensitive = false;
            for (const auto& key : sensitive_keys) {
                if (it.key() == key) { is_sensitive = true; break; }
            }
            if (is_sensitive && it->is_string() && !it->get<std::string>().empty()) {
                *it = "***REDACTED***";
            } else {
                redact_config_json(*it);
            }
        }
    } else if (j.is_array()) {
        for (auto& elem : j) { redact_config_json(elem); }
    }
}
}

TEST_CASE("Config value redaction", "[cli][redaction]") {
    SECTION("Redacts api_key") {
        json config = {{"providers", json::array({{{"name", "anthropic"}, {"api_key", "sk-ant-12345"}}})}};
        redact_config_json(config);
        CHECK(config["providers"][0]["api_key"] == "***REDACTED***");
        CHECK(config["providers"][0]["name"] == "anthropic");
    }
    SECTION("Redacts bot_token") {
        json config = {{"channels", json::array({{{"type", "telegram"}, {"bot_token", "123:ABC"}}})}};
        redact_config_json(config);
        CHECK(config["channels"][0]["bot_token"] == "***REDACTED***");
    }
    SECTION("Preserves non-sensitive keys") {
        json config = {{"log_level", "info"}, {"gateway", {{"port", 18789}}}};
        json original = config;
        redact_config_json(config);
        CHECK(config == original);
    }
    SECTION("Handles nested objects") {
        json config = {{"auth", {{"token", "my-secret-token"}}}};
        redact_config_json(config);
        CHECK(config["auth"]["token"] == "***REDACTED***");
    }
    SECTION("Skips empty strings") {
        json config = {{"api_key", ""}};
        redact_config_json(config);
        CHECK(config["api_key"] == "");
    }
}
