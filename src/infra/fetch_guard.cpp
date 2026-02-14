#include "openclaw/infra/fetch_guard.hpp"

#include "openclaw/core/logger.hpp"

#include <set>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace openclaw::infra {

namespace net = boost::asio;

// ---------------------------------------------------------------------------
// IP classification
// ---------------------------------------------------------------------------

auto FetchGuard::is_private_ipv4(uint8_t a, uint8_t b,
                                   uint8_t c, uint8_t d) -> bool {
    (void)d;
    // 10.0.0.0/8
    if (a == 10) return true;
    // 172.16.0.0/12
    if (a == 172 && b >= 16 && b <= 31) return true;
    // 192.168.0.0/16
    if (a == 192 && b == 168) return true;
    // 127.0.0.0/8 (loopback)
    if (a == 127) return true;
    // 169.254.0.0/16 (link-local)
    if (a == 169 && b == 254) return true;
    // 100.64.0.0/10 (CGNAT)
    if (a == 100 && b >= 64 && b <= 127) return true;
    // 0.0.0.0
    if (a == 0 && b == 0 && c == 0) return true;

    return false;
}

auto FetchGuard::is_private_ip(std::string_view ip) -> bool {
    std::string ip_str(ip);

    // Try parsing as IPv4
    boost::system::error_code ec;
    auto addr = net::ip::make_address(ip_str, ec);
    if (ec) {
        // Can't parse -> conservative: block it
        LOG_WARN("FetchGuard: cannot parse IP '{}': {}", ip, ec.message());
        return true;
    }

    if (addr.is_v4()) {
        auto v4 = addr.to_v4();
        auto bytes = v4.to_bytes();
        return is_private_ipv4(bytes[0], bytes[1], bytes[2], bytes[3]);
    }

    if (addr.is_v6()) {
        auto v6 = addr.to_v6();

        // ::1 (loopback)
        if (v6.is_loopback()) return true;

        // fc00::/7 (Unique Local Address)
        auto bytes = v6.to_bytes();
        if ((bytes[0] & 0xFE) == 0xFC) return true;

        // fe80::/10 (link-local)
        if (v6.is_link_local()) return true;

        // IPv4-mapped IPv6 (::ffff:x.x.x.x)
        if (v6.is_v4_mapped()) {
            auto v4 = v6.to_v4();
            auto v4b = v4.to_bytes();
            return is_private_ipv4(v4b[0], v4b[1], v4b[2], v4b[3]);
        }

        // :: (unspecified)
        if (v6.is_unspecified()) return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// URL helpers
// ---------------------------------------------------------------------------

auto FetchGuard::extract_hostname(std::string_view url) -> std::string {
    // Strip scheme
    auto pos = url.find("://");
    if (pos != std::string_view::npos) {
        url = url.substr(pos + 3);
    }

    // Strip path
    pos = url.find('/');
    if (pos != std::string_view::npos) {
        url = url.substr(0, pos);
    }

    // Strip port
    pos = url.find(':');
    if (pos != std::string_view::npos) {
        url = url.substr(0, pos);
    }

    // Strip userinfo
    pos = url.find('@');
    if (pos != std::string_view::npos) {
        url = url.substr(pos + 1);
    }

    return std::string(url);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

auto FetchGuard::validate_url(std::string_view url, boost::asio::io_context& ioc)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    std::string hostname = extract_hostname(url);
    if (hostname.empty()) {
        co_return std::unexpected(
            make_error(ErrorCode::InvalidArgument, "Empty hostname in URL"));
    }

    // Resolve hostname to IP addresses
    net::ip::tcp::resolver resolver(ioc);
    try {
        auto results = co_await resolver.async_resolve(
            hostname, "443", net::use_awaitable);

        for (const auto& entry : results) {
            auto addr = entry.endpoint().address().to_string();
            if (is_private_ip(addr)) {
                co_return std::unexpected(
                    make_error(ErrorCode::Forbidden,
                               "SSRF blocked: URL resolves to private IP",
                               hostname + " -> " + addr));
            }
        }
    } catch (const boost::system::system_error& e) {
        co_return std::unexpected(
            make_error(ErrorCode::ConnectionFailed,
                       "DNS resolution failed for SSRF check",
                       hostname + ": " + e.what()));
    }

    co_return openclaw::Result<void>{};
}

auto FetchGuard::safe_fetch(std::string_view url,
                             HttpClient& http,
                             boost::asio::io_context& ioc,
                             int max_redirects)
    -> boost::asio::awaitable<openclaw::Result<HttpResponse>>
{
    std::string current_url(url);
    std::set<std::string> visited;

    for (int i = 0; i <= max_redirects; ++i) {
        // Validate current URL
        auto valid = co_await validate_url(current_url, ioc);
        if (!valid) {
            co_return std::unexpected(valid.error());
        }

        // Loop detection
        if (visited.contains(current_url)) {
            co_return std::unexpected(
                make_error(ErrorCode::InvalidArgument,
                           "Redirect loop detected",
                           current_url));
        }
        visited.insert(current_url);

        // Fetch
        auto response = co_await http.get(current_url);
        if (!response) {
            co_return std::unexpected(response.error());
        }

        // Check for redirect (3xx)
        if (response->status >= 300 && response->status < 400) {
            // Extract Location header
            auto it = response->headers.find("location");
            if (it == response->headers.end()) {
                it = response->headers.find("Location");
            }
            if (it != response->headers.end() && !it->second.empty()) {
                current_url = it->second;
                LOG_DEBUG("FetchGuard: following redirect to {}", current_url);
                continue;
            }
        }

        co_return *response;
    }

    co_return std::unexpected(
        make_error(ErrorCode::InvalidArgument,
                   "Too many redirects",
                   "max=" + std::to_string(max_redirects)));
}

} // namespace openclaw::infra
