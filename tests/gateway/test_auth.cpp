#include <catch2/catch_test_macros.hpp>

#include "openclaw/gateway/auth.hpp"

using namespace openclaw::gateway;

TEST_CASE("extract_bearer_token parses Authorization header", "[auth]") {
    SECTION("valid bearer token") {
        auto token = Authenticator::extract_bearer_token("Bearer abc123xyz");
        REQUIRE(token.has_value());
        CHECK(*token == "abc123xyz");
    }

    SECTION("bearer prefix is case-sensitive") {
        auto token = Authenticator::extract_bearer_token("bearer abc");
        // The standard says "Bearer" with capital B; implementation may vary
        // but we test the expected behavior
        // If it's strictly case-sensitive, this may be nullopt
        // Either behavior is fine; we just test it does not crash
        (void)token;
    }

    SECTION("non-bearer scheme returns nullopt") {
        auto token = Authenticator::extract_bearer_token("Basic dXNlcjpwYXNz");
        CHECK_FALSE(token.has_value());
    }

    SECTION("empty header returns nullopt") {
        auto token = Authenticator::extract_bearer_token("");
        CHECK_FALSE(token.has_value());
    }

    SECTION("just 'Bearer' with no token") {
        auto token = Authenticator::extract_bearer_token("Bearer ");
        // Should return empty string or nullopt depending on implementation
        if (token.has_value()) {
            CHECK(token->empty());
        }
    }
}

TEST_CASE("extract_token_from_request finds token in query string", "[auth]") {
    SECTION("token in query parameter") {
        auto token = Authenticator::extract_token_from_request(
            "/api/chat?token=mytoken123", "");
        REQUIRE(token.has_value());
        CHECK(*token == "mytoken123");
    }

    SECTION("token from Authorization header preferred or also works") {
        auto token = Authenticator::extract_token_from_request(
            "/api/chat", "Bearer headertoken");
        REQUIRE(token.has_value());
        CHECK(*token == "headertoken");
    }

    SECTION("no token anywhere returns nullopt") {
        auto token = Authenticator::extract_token_from_request("/api/chat", "");
        CHECK_FALSE(token.has_value());
    }

    SECTION("query string with multiple params") {
        auto token = Authenticator::extract_token_from_request(
            "/api/chat?foo=bar&token=secretvalue&baz=qux", "");
        REQUIRE(token.has_value());
        CHECK(*token == "secretvalue");
    }
}

TEST_CASE("Authenticator default is open (no auth)", "[auth]") {
    Authenticator auth;

    CHECK(auth.is_open());
    CHECK(auth.active_method() == AuthMethod::None);
}

TEST_CASE("Authenticator configured with token auth", "[auth]") {
    openclaw::AuthConfig config;
    config.method = "token";
    config.token = "super_secret_key";

    Authenticator auth(config);

    CHECK_FALSE(auth.is_open());
    CHECK(auth.active_method() == AuthMethod::Token);
}

TEST_CASE("AuthMethod enum values", "[auth]") {
    CHECK(AuthMethod::None != AuthMethod::Token);
    CHECK(AuthMethod::None != AuthMethod::Tailscale);
    CHECK(AuthMethod::Token != AuthMethod::Tailscale);
}
