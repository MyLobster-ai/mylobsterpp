#include "openclaw/gateway/auth.hpp"

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <algorithm>

#include <boost/asio/use_awaitable.hpp>

namespace openclaw::gateway {

// -- AuthInfo serialization --

void to_json(json& j, const AuthInfo& a) {
    j = json{
        {"identity", a.identity},
        {"method", static_cast<int>(a.method)},
        {"metadata", a.metadata},
    };
    if (a.device) j["device"] = *a.device;
}

void from_json(const json& j, AuthInfo& a) {
    j.at("identity").get_to(a.identity);
    if (j.contains("method")) a.method = static_cast<AuthMethod>(j["method"].get<int>());
    if (j.contains("device")) a.device = j["device"].get<std::string>();
    if (j.contains("metadata")) a.metadata = j["metadata"];
}

// -- TokenAuthVerifier --

TokenAuthVerifier::TokenAuthVerifier(std::string secret)
    : secret_(std::move(secret)) {}

auto TokenAuthVerifier::verify(std::string_view credential)
    -> awaitable<Result<AuthInfo>> {
    // Constant-time comparison to prevent timing attacks.
    if (credential.size() != secret_.size()) {
        co_return std::unexpected(
            make_error(ErrorCode::Unauthorized, "Invalid authentication token"));
    }

    volatile bool equal = true;
    for (size_t i = 0; i < credential.size(); ++i) {
        equal &= (credential[i] == secret_[i]);
    }

    if (!equal) {
        co_return std::unexpected(
            make_error(ErrorCode::Unauthorized, "Invalid authentication token"));
    }

    co_return AuthInfo{
        .identity = "token-user",
        .method = AuthMethod::Token,
        .device = std::nullopt,
        .metadata = json::object(),
    };
}

// -- TailscaleAuthVerifier --

TailscaleAuthVerifier::TailscaleAuthVerifier(std::string socket_path)
    : socket_path_(std::move(socket_path)) {
    if (socket_path_.empty()) {
        // Default tailscale socket path on Linux/macOS.
        socket_path_ = "/var/run/tailscale/tailscaled.sock";
    }
}

auto TailscaleAuthVerifier::verify(std::string_view peer_addr)
    -> awaitable<Result<AuthInfo>> {
    // In a full implementation this would connect to the tailscale local API
    // (/localapi/v0/whois?addr=<peer_addr>) via the Unix domain socket and
    // extract the peer's identity (login name, node name, etc.).
    //
    // For now, return a stub result indicating where the real implementation
    // would go.

    if (peer_addr.empty()) {
        co_return std::unexpected(
            make_error(ErrorCode::Unauthorized,
                       "Empty peer address for tailscale auth"));
    }

    LOG_DEBUG("Tailscale auth: would verify peer {} via {}",
              std::string(peer_addr), socket_path_);

    // Stub: accept the connection and use the peer address as identity.
    co_return AuthInfo{
        .identity = std::string(peer_addr),
        .method = AuthMethod::Tailscale,
        .device = std::nullopt,
        .metadata = json{{"socket", socket_path_}},
    };
}

// -- Authenticator --

Authenticator::Authenticator() = default;

Authenticator::Authenticator(const AuthConfig& config) {
    configure(config);
}

void Authenticator::configure(const AuthConfig& config) {
    if (config.method == "token") {
        if (!config.token || config.token->empty()) {
            LOG_WARN("Token auth configured but no token provided; falling back to none");
            method_ = AuthMethod::None;
            verifier_.reset();
            return;
        }
        method_ = AuthMethod::Token;
        verifier_ = std::make_unique<TokenAuthVerifier>(*config.token);
        LOG_INFO("Gateway auth: token-based authentication enabled");
    } else if (config.method == "tailscale") {
        method_ = AuthMethod::Tailscale;
        std::string sock;
        if (config.tailscale_authkey) sock = *config.tailscale_authkey;
        verifier_ = std::make_unique<TailscaleAuthVerifier>(std::move(sock));
        LOG_INFO("Gateway auth: tailscale authentication enabled");
    } else {
        method_ = AuthMethod::None;
        verifier_.reset();
        LOG_INFO("Gateway auth: no authentication (open access)");
    }
}

auto Authenticator::verify(std::string_view credential)
    -> awaitable<Result<AuthInfo>> {
    if (method_ == AuthMethod::None) {
        // No auth: return an anonymous identity.
        co_return AuthInfo{
            .identity = "anonymous",
            .method = AuthMethod::None,
            .device = std::nullopt,
            .metadata = json::object(),
        };
    }

    if (!verifier_) {
        co_return std::unexpected(
            make_error(ErrorCode::InternalError,
                       "Auth verifier not configured"));
    }

    co_return co_await verifier_->verify(credential);
}

auto Authenticator::extract_bearer_token(std::string_view header_value)
    -> std::optional<std::string_view> {
    constexpr std::string_view prefix = "Bearer ";
    if (header_value.size() <= prefix.size()) return std::nullopt;

    if (header_value.substr(0, prefix.size()) != prefix) return std::nullopt;

    auto token = header_value.substr(prefix.size());
    // Trim whitespace.
    while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
    while (!token.empty() && token.back() == ' ') token.remove_suffix(1);

    if (token.empty()) return std::nullopt;
    return token;
}

auto Authenticator::extract_token_from_request(std::string_view target,
                                               std::string_view auth_header)
    -> std::optional<std::string_view> {
    // Try Authorization header first.
    if (!auth_header.empty()) {
        auto token = extract_bearer_token(auth_header);
        if (token) return token;
    }

    // Fall back to ?token= query parameter.
    auto qpos = target.find('?');
    if (qpos == std::string_view::npos) return std::nullopt;

    auto query = target.substr(qpos + 1);
    constexpr std::string_view key = "token=";

    size_t pos = 0;
    while (pos < query.size()) {
        auto amp = query.find('&', pos);
        auto segment = (amp == std::string_view::npos)
            ? query.substr(pos)
            : query.substr(pos, amp - pos);

        if (segment.size() > key.size() &&
            segment.substr(0, key.size()) == key) {
            return segment.substr(key.size());
        }

        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }

    return std::nullopt;
}

auto Authenticator::is_open() const noexcept -> bool {
    return method_ == AuthMethod::None;
}

auto Authenticator::active_method() const noexcept -> AuthMethod {
    return method_;
}

} // namespace openclaw::gateway
