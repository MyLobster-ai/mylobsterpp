#include <catch2/catch_test_macros.hpp>
#include "openclaw/sessions/session.hpp"
using namespace openclaw::sessions;

TEST_CASE("Credential redaction", "[sessions][redaction]") {
    SECTION("Redacts API keys") {
        auto result = redact_credentials(R"(Use api_key="sk-abc123def456ghi789")");
        CHECK(result.find("sk-abc123def456ghi789") == std::string::npos);
        CHECK(result.find("REDACTED") != std::string::npos);
    }
    SECTION("Redacts Bearer tokens") {
        auto result = redact_credentials("Authorization: Bearer eyJhbGciOiJIUzI1NiJ9.test");
        CHECK(result.find("eyJhbGciOiJIUzI1NiJ9") == std::string::npos);
        CHECK(result.find("REDACTED") != std::string::npos);
    }
    SECTION("Redacts sk- prefixed keys") {
        auto result = redact_credentials("key is sk-proj-abcdef1234567890");
        CHECK(result.find("sk-proj-abcdef1234567890") == std::string::npos);
    }
    SECTION("Preserves normal text") {
        auto text = "This is a normal message about API design";
        CHECK(redact_credentials(text) == text);
    }
}

TEST_CASE("Metadata stripping", "[sessions][redaction]") {
    SECTION("Strips metadata blocks") {
        auto result = strip_inbound_metadata("Hello <!-- metadata: {\"role\":\"system\"} --> World");
        CHECK(result.find("metadata") == std::string::npos);
        CHECK(result.find("Hello") != std::string::npos);
        CHECK(result.find("World") != std::string::npos);
    }
    SECTION("Preserves normal HTML comments") {
        auto text = "<!-- regular comment --> content";
        CHECK(strip_inbound_metadata(text) == text);
    }
}
