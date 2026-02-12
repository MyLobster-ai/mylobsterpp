#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/core/config.hpp"
#include "openclaw/core/error.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

/// Authentication methods supported by the gateway.
enum class AuthMethod {
    None,       // No authentication required
    Token,      // Shared secret / bearer token
    Tailscale,  // Tailscale identity (whois-based)
};

/// Information extracted after successful authentication.
struct AuthInfo {
    std::string identity;               // user id, email, or tailscale identity
    AuthMethod method = AuthMethod::None;
    std::optional<std::string> device;  // optional device/node info
    json metadata;                      // extra provider-specific data
};

void to_json(json& j, const AuthInfo& a);
void from_json(const json& j, AuthInfo& a);

/// Abstract base for authentication verification.
class AuthVerifier {
public:
    virtual ~AuthVerifier() = default;

    /// Verify the given token/credential string and return AuthInfo on success.
    virtual auto verify(std::string_view credential)
        -> awaitable<Result<AuthInfo>> = 0;

    /// Return the auth method this verifier handles.
    [[nodiscard]] virtual auto method() const noexcept -> AuthMethod = 0;
};

/// Token-based authentication.  Compares a bearer token against a stored
/// secret using constant-time comparison.
class TokenAuthVerifier final : public AuthVerifier {
public:
    explicit TokenAuthVerifier(std::string secret);

    auto verify(std::string_view credential)
        -> awaitable<Result<AuthInfo>> override;

    [[nodiscard]] auto method() const noexcept -> AuthMethod override {
        return AuthMethod::Token;
    }

private:
    std::string secret_;
};

/// Tailscale-based authentication.  Calls the local tailscale daemon
/// to verify the connecting peer's identity.
class TailscaleAuthVerifier final : public AuthVerifier {
public:
    explicit TailscaleAuthVerifier(std::string socket_path = "");

    auto verify(std::string_view peer_addr)
        -> awaitable<Result<AuthInfo>> override;

    [[nodiscard]] auto method() const noexcept -> AuthMethod override {
        return AuthMethod::Tailscale;
    }

private:
    std::string socket_path_;
};

/// The Authenticator orchestrates authentication for the gateway.
/// It is configured from AuthConfig and delegates to the appropriate
/// verifier.
class Authenticator {
public:
    Authenticator();
    explicit Authenticator(const AuthConfig& config);

    /// Configure authentication from an AuthConfig.
    void configure(const AuthConfig& config);

    /// Verify a credential (token string, peer address, etc.).
    auto verify(std::string_view credential)
        -> awaitable<Result<AuthInfo>>;

    /// Extract a bearer token from an HTTP Authorization header value.
    static auto extract_bearer_token(std::string_view header_value)
        -> std::optional<std::string_view>;

    /// Extract a token from a WebSocket upgrade request's query string
    /// (?token=...) or Authorization header.
    static auto extract_token_from_request(std::string_view target,
                                           std::string_view auth_header)
        -> std::optional<std::string_view>;

    /// Return true if authentication is disabled (method == None).
    [[nodiscard]] auto is_open() const noexcept -> bool;

    /// Return the active authentication method.
    [[nodiscard]] auto active_method() const noexcept -> AuthMethod;

private:
    AuthMethod method_ = AuthMethod::None;
    std::unique_ptr<AuthVerifier> verifier_;
};

} // namespace openclaw::gateway
