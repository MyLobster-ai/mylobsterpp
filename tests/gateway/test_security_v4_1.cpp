#include <catch2/catch_test_macros.hpp>

#include "openclaw/gateway/auth.hpp"

using namespace openclaw::gateway;
using namespace openclaw;

TEST_CASE("validate_trusted_proxy_config (v2026.3.31)", "[gateway][security]") {
    SECTION("rejects same token for gateway and proxy") {
        AuthConfig auth;
        auth.method = "token";
        auth.token = "shared-secret";
        // Same token should be rejected
        CHECK_FALSE(validate_trusted_proxy_config(auth, "shared-secret"));
    }

    SECTION("accepts different tokens") {
        AuthConfig auth;
        auth.method = "token";
        auth.token = "gateway-token";
        CHECK(validate_trusted_proxy_config(auth, "proxy-token"));
    }

    SECTION("accepts when proxy token not provided") {
        AuthConfig auth;
        auth.method = "token";
        auth.token = "gateway-token";
        CHECK(validate_trusted_proxy_config(auth));
    }

    SECTION("rejects token auth without configured token") {
        AuthConfig auth;
        auth.method = "token";
        // No token configured
        CHECK_FALSE(validate_trusted_proxy_config(auth));
    }

    SECTION("accepts non-token auth methods") {
        AuthConfig auth;
        auth.method = "none";
        CHECK(validate_trusted_proxy_config(auth));
    }
}

TEST_CASE("should_allow_node_commands (v2026.3.31)", "[gateway][security]") {
    SECTION("blocks commands when pairing not approved") {
        AuthInfo auth;
        auth.method = AuthMethod::Token;
        auth.identity = "node-user";
        CHECK_FALSE(should_allow_node_commands(auth, false));
    }

    SECTION("allows commands when pairing is approved") {
        AuthInfo auth;
        auth.method = AuthMethod::Token;
        auth.identity = "node-user";
        CHECK(should_allow_node_commands(auth, true));
    }

    SECTION("blocks commands with no authentication") {
        AuthInfo auth;
        auth.method = AuthMethod::None;
        CHECK_FALSE(should_allow_node_commands(auth, true));
    }
}

TEST_CASE("is_node_run_restricted (v2026.3.31)", "[gateway][security]") {
    SECTION("restricts node-originated runs") {
        AuthInfo auth;
        auth.metadata = nlohmann::json{{"node_origin", true}};
        CHECK(is_node_run_restricted(auth));
    }

    SECTION("does not restrict non-node runs") {
        AuthInfo auth;
        auth.metadata = nlohmann::json::object();
        CHECK_FALSE(is_node_run_restricted(auth));
    }

    SECTION("does not restrict when node_origin is false") {
        AuthInfo auth;
        auth.metadata = nlohmann::json{{"node_origin", false}};
        CHECK_FALSE(is_node_run_restricted(auth));
    }
}
