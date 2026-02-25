#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>

#include "openclaw/core/error.hpp"
#include "openclaw/infra/http_client.hpp"

namespace openclaw::infra {

/// SSRF protection for outbound HTTP requests.
/// Validates URLs against private/reserved IP ranges before allowing fetches.
///
/// In v2026.2.23+, the default policy changed to trusted-network mode:
/// private IPs are allowed unless explicitly disabled. This supports local
/// development and trusted network deployments where browser tools need to
/// access localhost, 192.168.x.x, 10.x.x.x, etc.
class FetchGuard {
public:
    /// Default constructor: trusted-network mode (allow private IPs).
    FetchGuard() = default;

    /// Construct with explicit SSRF policy.
    /// When allow_private is true, skips private IP validation.
    explicit FetchGuard(bool allow_private) : allow_private_(allow_private) {}

    /// Checks if an IP address string is in a private/reserved range.
    /// Blocks: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 127.0.0.0/8,
    ///         169.254.0.0/16, 100.64.0.0/10, fc00::/7, ::1
    [[nodiscard]] static auto is_private_ip(std::string_view ip) -> bool;

    /// Validates a URL by resolving its hostname and checking against blocked ranges.
    /// Returns an error if the URL resolves to a private IP.
    auto validate_url(std::string_view url, boost::asio::io_context& ioc)
        -> boost::asio::awaitable<openclaw::Result<void>>;

    /// Fetches a URL safely with SSRF protection.
    /// Validates the URL, follows up to max_redirects redirects (with loop detection),
    /// and validates each redirect target.
    auto safe_fetch(std::string_view url,
                    HttpClient& http,
                    boost::asio::io_context& ioc,
                    int max_redirects = 3)
        -> boost::asio::awaitable<openclaw::Result<HttpResponse>>;

    /// Extracts the origin (scheme + host + port) from a URL.
    [[nodiscard]] static auto extract_origin(std::string_view url) -> std::string;

    /// Strips sensitive headers when a redirect crosses origin boundaries.
    static void strip_cross_origin_headers(
        std::map<std::string, std::string>& headers,
        std::string_view from_url, std::string_view to_url);

    /// Sanitizes HTML content by removing hidden/invisible elements that could
    /// contain prompt injection payloads. Strips elements with display:none,
    /// visibility:hidden, sr-only class, aria-hidden, opacity:0, font-size:0.
    [[nodiscard]] static auto sanitize_html_content(std::string_view html) -> std::string;

    /// Returns true if private IP access is currently allowed (trusted-network mode).
    [[nodiscard]] auto allows_private() const noexcept -> bool { return allow_private_; }

private:
    /// Extracts hostname from a URL string.
    [[nodiscard]] static auto extract_hostname(std::string_view url) -> std::string;

    /// Checks if an IPv4 address (as 4 octets) is private.
    [[nodiscard]] static auto is_private_ipv4(uint8_t a, uint8_t b,
                                                uint8_t c, uint8_t d) -> bool;

    bool allow_private_ = true;  // v2026.2.23+: default trusted-network mode
};

} // namespace openclaw::infra
