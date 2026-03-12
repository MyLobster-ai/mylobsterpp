#include <catch2/catch_test_macros.hpp>
#include "openclaw/gateway/server.hpp"

using namespace openclaw::gateway;

TEST_CASE("validate_ws_origin GHSA-5wcw-8jjv-m286", "[gateway][security]") {
    SECTION("non-browser clients (no origin) are allowed") {
        CHECK(GatewayServer::validate_ws_origin("", "localhost:18789"));
        CHECK(GatewayServer::validate_ws_origin("", "example.com"));
    }

    SECTION("matching origins are allowed") {
        CHECK(GatewayServer::validate_ws_origin("http://localhost:3000", "localhost:18789"));
        CHECK(GatewayServer::validate_ws_origin("http://127.0.0.1:3000", "127.0.0.1:18789"));
        CHECK(GatewayServer::validate_ws_origin("https://example.com", "example.com:443"));
    }

    SECTION("localhost variants always allowed") {
        CHECK(GatewayServer::validate_ws_origin("http://localhost:3000", "example.com"));
        CHECK(GatewayServer::validate_ws_origin("http://127.0.0.1:9000", "example.com"));
        CHECK(GatewayServer::validate_ws_origin("http://[::1]:9000", "example.com"));
    }

    SECTION("cross-site origins are rejected") {
        CHECK_FALSE(GatewayServer::validate_ws_origin("http://evil.com", "example.com"));
        CHECK_FALSE(GatewayServer::validate_ws_origin("https://attacker.io", "mygateway.com"));
    }

    SECTION("trusted_proxy does NOT bypass validation (GHSA fix)") {
        CHECK_FALSE(GatewayServer::validate_ws_origin("http://evil.com", "example.com", true));
    }

    SECTION("malformed origins rejected") {
        CHECK_FALSE(GatewayServer::validate_ws_origin("notaurl", "example.com"));
    }
}
