#include <catch2/catch_test_macros.hpp>

#include "openclaw/core/config.hpp"
#include "openclaw/infra/fetch_guard.hpp"

using namespace openclaw::infra;

TEST_CASE("FetchGuard private IP detection", "[infra][fetch_guard]") {
    SECTION("RFC 1918 - 10.0.0.0/8") {
        CHECK(FetchGuard::is_private_ip("10.0.0.1"));
        CHECK(FetchGuard::is_private_ip("10.255.255.255"));
        CHECK(FetchGuard::is_private_ip("10.1.2.3"));
    }

    SECTION("RFC 1918 - 172.16.0.0/12") {
        CHECK(FetchGuard::is_private_ip("172.16.0.1"));
        CHECK(FetchGuard::is_private_ip("172.31.255.255"));
        CHECK_FALSE(FetchGuard::is_private_ip("172.15.255.255"));
        CHECK_FALSE(FetchGuard::is_private_ip("172.32.0.1"));
    }

    SECTION("RFC 1918 - 192.168.0.0/16") {
        CHECK(FetchGuard::is_private_ip("192.168.0.1"));
        CHECK(FetchGuard::is_private_ip("192.168.255.255"));
        CHECK_FALSE(FetchGuard::is_private_ip("192.167.0.1"));
    }

    SECTION("Loopback - 127.0.0.0/8") {
        CHECK(FetchGuard::is_private_ip("127.0.0.1"));
        CHECK(FetchGuard::is_private_ip("127.255.255.255"));
    }

    SECTION("Link-local - 169.254.0.0/16") {
        CHECK(FetchGuard::is_private_ip("169.254.0.1"));
        CHECK(FetchGuard::is_private_ip("169.254.169.254"));
    }

    SECTION("CGNAT - 100.64.0.0/10") {
        CHECK(FetchGuard::is_private_ip("100.64.0.1"));
        CHECK(FetchGuard::is_private_ip("100.127.255.255"));
        CHECK_FALSE(FetchGuard::is_private_ip("100.63.255.255"));
        CHECK_FALSE(FetchGuard::is_private_ip("100.128.0.1"));
    }

    SECTION("Public IPs are not private") {
        CHECK_FALSE(FetchGuard::is_private_ip("8.8.8.8"));
        CHECK_FALSE(FetchGuard::is_private_ip("1.1.1.1"));
        CHECK_FALSE(FetchGuard::is_private_ip("142.250.80.46"));
        CHECK_FALSE(FetchGuard::is_private_ip("93.184.216.34"));
    }

    SECTION("IPv6 loopback") {
        CHECK(FetchGuard::is_private_ip("::1"));
    }

    SECTION("IPv6 ULA - fc00::/7") {
        CHECK(FetchGuard::is_private_ip("fc00::1"));
        CHECK(FetchGuard::is_private_ip("fd00::1"));
    }

    SECTION("IPv6 link-local - fe80::/10") {
        CHECK(FetchGuard::is_private_ip("fe80::1"));
    }

    SECTION("IPv6 public addresses") {
        CHECK_FALSE(FetchGuard::is_private_ip("2001:4860:4860::8888"));
        CHECK_FALSE(FetchGuard::is_private_ip("2606:4700:4700::1111"));
    }

    SECTION("Invalid IP returns true (conservative)") {
        CHECK(FetchGuard::is_private_ip("not-an-ip"));
    }
}

TEST_CASE("FetchGuard cross-origin header stripping", "[infra][fetch_guard]") {
    SECTION("Same origin preserves headers") {
        std::map<std::string, std::string> headers = {
            {"Authorization", "Bearer token123"},
            {"Cookie", "session=abc"},
            {"Accept", "text/html"},
        };
        FetchGuard::strip_cross_origin_headers(headers,
            "https://example.com/path1", "https://example.com/path2");
        CHECK(headers.contains("Authorization"));
        CHECK(headers.contains("Cookie"));
        CHECK(headers.contains("Accept"));
    }

    SECTION("Cross-origin strips sensitive headers") {
        std::map<std::string, std::string> headers = {
            {"Authorization", "Bearer token123"},
            {"Cookie", "session=abc"},
            {"Proxy-Authorization", "Basic xyz"},
            {"Accept", "text/html"},
        };
        FetchGuard::strip_cross_origin_headers(headers,
            "https://example.com/path", "https://evil.com/path");
        CHECK_FALSE(headers.contains("Authorization"));
        CHECK_FALSE(headers.contains("Cookie"));
        CHECK_FALSE(headers.contains("Proxy-Authorization"));
        CHECK(headers.contains("Accept"));
    }

    SECTION("Different port is cross-origin") {
        std::map<std::string, std::string> headers = {
            {"Authorization", "Bearer token"},
        };
        FetchGuard::strip_cross_origin_headers(headers,
            "https://example.com:443/path", "https://example.com:8443/path");
        CHECK_FALSE(headers.contains("Authorization"));
    }
}

TEST_CASE("FetchGuard origin extraction", "[infra][fetch_guard]") {
    CHECK(FetchGuard::extract_origin("https://example.com/path") == "https://example.com");
    CHECK(FetchGuard::extract_origin("https://example.com:8443/path") == "https://example.com:8443");
    CHECK(FetchGuard::extract_origin("http://localhost:3000/api") == "http://localhost:3000");
}

// ---------------------------------------------------------------------------
// v2026.2.23: SSRF policy default change (trusted-network mode)
// ---------------------------------------------------------------------------

TEST_CASE("FetchGuard default constructor enables trusted-network mode", "[infra][fetch_guard][ssrf_policy]") {
    FetchGuard guard;
    CHECK(guard.allows_private() == true);
}

TEST_CASE("FetchGuard explicit allow_private=false blocks private IPs", "[infra][fetch_guard][ssrf_policy]") {
    FetchGuard guard(false);
    CHECK(guard.allows_private() == false);
}

TEST_CASE("FetchGuard explicit allow_private=true allows private IPs", "[infra][fetch_guard][ssrf_policy]") {
    FetchGuard guard(true);
    CHECK(guard.allows_private() == true);
}

// ---------------------------------------------------------------------------
// SsrfPolicyConfig resolution
// ---------------------------------------------------------------------------

TEST_CASE("SsrfPolicyConfig defaults to true when nothing set", "[infra][ssrf_policy]") {
    openclaw::SsrfPolicyConfig policy;
    CHECK(openclaw::resolve_ssrf_allow_private(policy) == true);
}

TEST_CASE("SsrfPolicyConfig canonical key takes precedence", "[infra][ssrf_policy]") {
    openclaw::SsrfPolicyConfig policy;
    policy.allow_private_network = true;
    policy.dangerously_allow_private_network = false;
    CHECK(openclaw::resolve_ssrf_allow_private(policy) == false);
}

TEST_CASE("SsrfPolicyConfig legacy key respected when canonical not set", "[infra][ssrf_policy]") {
    openclaw::SsrfPolicyConfig policy;
    policy.allow_private_network = false;
    CHECK(openclaw::resolve_ssrf_allow_private(policy) == false);
}

TEST_CASE("BrowserConfig includes ssrf_policy", "[infra][ssrf_policy]") {
    openclaw::BrowserConfig browser;
    CHECK(openclaw::resolve_ssrf_allow_private(browser.ssrf_policy) == true);
}
