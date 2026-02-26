#include "openclaw/infra/fetch_guard.hpp"

#include "openclaw/core/logger.hpp"

#include <algorithm>
#include <regex>
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

        // v2026.2.25: ff00::/8 (multicast) — blocks SSRF via multicast addresses
        if ((bytes[0] & 0xFF) == 0xFF) return true;

        // IPv4-mapped IPv6 (::ffff:x.x.x.x) — IPv4 octets at bytes 12-15
        if (v6.is_v4_mapped()) {
            return is_private_ipv4(bytes[12], bytes[13], bytes[14], bytes[15]);
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
// Origin extraction and cross-origin header stripping
// ---------------------------------------------------------------------------

auto FetchGuard::extract_origin(std::string_view url) -> std::string {
    // Extract scheme
    auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return std::string(url);
    }

    std::string_view scheme = url.substr(0, scheme_end);
    std::string_view rest = url.substr(scheme_end + 3);

    // Strip userinfo
    auto at_pos = rest.find('@');
    auto slash_pos = rest.find('/');
    if (at_pos != std::string_view::npos &&
        (slash_pos == std::string_view::npos || at_pos < slash_pos)) {
        rest = rest.substr(at_pos + 1);
    }

    // Extract host:port (stop at path, query, or fragment)
    auto end_pos = rest.find_first_of("/?#");
    std::string_view host_port = (end_pos != std::string_view::npos)
        ? rest.substr(0, end_pos)
        : rest;

    return std::string(scheme) + "://" + std::string(host_port);
}

void FetchGuard::strip_cross_origin_headers(
    std::map<std::string, std::string>& headers,
    std::string_view from_url, std::string_view to_url)
{
    auto from_origin = extract_origin(from_url);
    auto to_origin = extract_origin(to_url);

    if (from_origin == to_origin) {
        return;
    }

    static const std::vector<std::string> sensitive_headers = {
        "Authorization", "Cookie", "Proxy-Authorization",
    };

    for (const auto& header : sensitive_headers) {
        headers.erase(header);
    }
}

// ---------------------------------------------------------------------------
// HTML content sanitization
// ---------------------------------------------------------------------------

auto FetchGuard::sanitize_html_content(std::string_view html) -> std::string {
    std::string result(html);

    // Remove elements with hidden styles/attributes
    // Pattern: tags containing display:none, visibility:hidden, etc.
    static const std::regex hidden_patterns[] = {
        std::regex(R"(<[^>]*\bstyle\s*=\s*"[^"]*display\s*:\s*none[^"]*"[^>]*>.*?</[^>]+>)", std::regex::icase),
        std::regex(R"(<[^>]*\bstyle\s*=\s*"[^"]*visibility\s*:\s*hidden[^"]*"[^>]*>.*?</[^>]+>)", std::regex::icase),
        std::regex(R"(<[^>]*\bclass\s*=\s*"[^"]*\bsr-only\b[^"]*"[^>]*>.*?</[^>]+>)", std::regex::icase),
        std::regex(R"(<[^>]*\baria-hidden\s*=\s*"true"[^>]*>.*?</[^>]+>)", std::regex::icase),
        std::regex(R"(<[^>]*\bstyle\s*=\s*"[^"]*opacity\s*:\s*0[^"]*"[^>]*>.*?</[^>]+>)", std::regex::icase),
        std::regex(R"(<[^>]*\bstyle\s*=\s*"[^"]*font-size\s*:\s*0[^"]*"[^>]*>.*?</[^>]+>)", std::regex::icase),
    };

    for (const auto& pattern : hidden_patterns) {
        result = std::regex_replace(result, pattern, "");
    }

    return result;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

auto FetchGuard::validate_url(std::string_view url, boost::asio::io_context& ioc)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    std::string hostname = extract_hostname(url);
    if (hostname.empty()) {
        co_return make_fail(
            make_error(ErrorCode::InvalidArgument, "Empty hostname in URL"));
    }

    // v2026.2.23+: skip private IP validation in trusted-network mode
    if (allow_private_) {
        co_return ok_result();
    }

    // Resolve hostname to IP addresses
    net::ip::tcp::resolver resolver(ioc);
    try {
        auto results = co_await resolver.async_resolve(
            hostname, "443", net::use_awaitable);

        // Sort resolved addresses with IPv4 first for consistent behavior
        std::vector<std::string> addresses;
        for (const auto& entry : results) {
            addresses.push_back(entry.endpoint().address().to_string());
        }
        std::sort(addresses.begin(), addresses.end(),
                  [](const std::string& a, const std::string& b) {
                      bool a_v4 = a.find(':') == std::string::npos;
                      bool b_v4 = b.find(':') == std::string::npos;
                      if (a_v4 != b_v4) return a_v4;  // IPv4 first
                      return a < b;
                  });

        for (const auto& addr : addresses) {
            if (is_private_ip(addr)) {
                co_return make_fail(
                    make_error(ErrorCode::Forbidden,
                               "SSRF blocked: URL resolves to private IP",
                               hostname + " -> " + addr));
            }
        }
    } catch (const boost::system::system_error& e) {
        co_return make_fail(
            make_error(ErrorCode::ConnectionFailed,
                       "DNS resolution failed for SSRF check",
                       hostname + ": " + e.what()));
    }

    co_return ok_result();
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
            co_return make_fail(valid.error());
        }

        // Loop detection
        if (visited.contains(current_url)) {
            co_return make_fail(
                make_error(ErrorCode::InvalidArgument,
                           "Redirect loop detected",
                           current_url));
        }
        visited.insert(current_url);

        // Fetch
        auto response = co_await http.get(current_url);
        if (!response) {
            co_return make_fail(response.error());
        }

        // Check for redirect (3xx)
        if (response->status >= 300 && response->status < 400) {
            // Extract Location header
            auto it = response->headers.find("location");
            if (it == response->headers.end()) {
                it = response->headers.find("Location");
            }
            if (it != response->headers.end() && !it->second.empty()) {
                std::string previous_url = current_url;
                current_url = it->second;

                // Check for cross-origin redirect and warn about header stripping
                auto from_origin = extract_origin(previous_url);
                auto to_origin = extract_origin(current_url);
                if (from_origin != to_origin) {
                    LOG_WARN("FetchGuard: cross-origin redirect detected from {} to {}, "
                             "sensitive headers (Authorization, Cookie, Proxy-Authorization) "
                             "would be stripped",
                             from_origin, to_origin);
                }

                LOG_DEBUG("FetchGuard: following redirect to {}", current_url);
                continue;
            }
        }

        co_return *response;
    }

    co_return make_fail(
        make_error(ErrorCode::InvalidArgument,
                   "Too many redirects",
                   "max=" + std::to_string(max_redirects)));
}

} // namespace openclaw::infra
