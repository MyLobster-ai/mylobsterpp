#pragma once

#include <chrono>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"

namespace openclaw::infra {

using json = nlohmann::json;

/// Creates an HS256-signed JWT with the given claims, secret, and expiry duration.
/// Standard claims (iat, exp) are automatically added.
/// The `claims` object is merged into the token payload.
auto create_token(const json& claims,
                  std::string_view secret,
                  std::chrono::seconds expiry = std::chrono::hours{24})
    -> std::string;

/// Verifies an HS256-signed JWT and returns the decoded payload claims.
/// Returns an error if the token is expired, malformed, or the signature is invalid.
auto verify_token(std::string_view token,
                  std::string_view secret)
    -> openclaw::Result<json>;

/// Extracts the payload from a JWT without verifying the signature.
/// Useful for inspecting claims before verification.
/// Returns an error if the token is malformed.
auto decode_token_unverified(std::string_view token)
    -> openclaw::Result<json>;

} // namespace openclaw::infra
