#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>

#include "openclaw/core/error.hpp"
#include "openclaw/infra/http_client.hpp"

namespace openclaw::infra {

/// SSRF protection for outbound HTTP requests.
/// Validates URLs against private/reserved IP ranges before allowing fetches.
class FetchGuard {
public:
    FetchGuard() = default;

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

private:
    /// Extracts hostname from a URL string.
    [[nodiscard]] static auto extract_hostname(std::string_view url) -> std::string;

    /// Checks if an IPv4 address (as 4 octets) is private.
    [[nodiscard]] static auto is_private_ipv4(uint8_t a, uint8_t b,
                                                uint8_t c, uint8_t d) -> bool;
};

} // namespace openclaw::infra
