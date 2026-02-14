#include <catch2/catch_test_macros.hpp>

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
