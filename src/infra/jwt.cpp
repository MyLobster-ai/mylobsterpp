#include "openclaw/infra/jwt.hpp"
#include "openclaw/core/logger.hpp"

#ifndef JWT_DISABLE_PICOJSON
#define JWT_DISABLE_PICOJSON
#endif

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>

namespace openclaw::infra {

// Type aliases for the nlohmann_json traits
using nl_traits = jwt::traits::nlohmann_json;
using nl_claim = jwt::basic_claim<nl_traits>;

auto create_token(const json& claims,
                  std::string_view secret,
                  std::chrono::seconds expiry) -> std::string {
    auto now = std::chrono::system_clock::now();

    auto builder = jwt::builder<nl_traits>()
        .set_type("JWT")
        .set_issued_at(now)
        .set_expires_at(now + expiry);

    // Merge all custom claims from the JSON object into the token
    for (auto it = claims.begin(); it != claims.end(); ++it) {
        const auto& key = it.key();
        const auto& val = it.value();

        // Use the json value directly — nl_claim accepts nlohmann::json
        builder.set_payload_claim(key, nl_claim(val));
    }

    auto token = builder.sign(jwt::algorithm::hs256{std::string(secret)});
    LOG_DEBUG("Created JWT token with {} claims, expires in {}s",
              claims.size(), expiry.count());
    return token;
}

auto verify_token(std::string_view token,
                  std::string_view secret) -> openclaw::Result<json> {
    try {
        auto verifier = jwt::verify<nl_traits>()
            .allow_algorithm(jwt::algorithm::hs256{std::string(secret)})
            .leeway(5);       // 5 second leeway for clock skew

        auto decoded = jwt::decode<nl_traits>(std::string(token));
        verifier.verify(decoded);

        // get_payload_json() returns the claims as a json object
        json payload = decoded.get_payload_json();

        LOG_DEBUG("JWT token verified successfully");
        return payload;

    } catch (const jwt::error::signature_verification_exception& e) {
        LOG_WARN("JWT signature verification failed: {}", e.what());
        return std::unexpected(
            openclaw::make_error(ErrorCode::Unauthorized,
                                "JWT signature verification failed", e.what()));
    } catch (const jwt::error::token_verification_exception& e) {
        LOG_WARN("JWT verification failed: {}", e.what());
        return std::unexpected(
            openclaw::make_error(ErrorCode::Unauthorized,
                                "JWT verification failed", e.what()));
    } catch (const std::exception& e) {
        LOG_ERROR("JWT decode error: {}", e.what());
        return std::unexpected(
            openclaw::make_error(ErrorCode::InvalidArgument,
                                "Invalid JWT token", e.what()));
    }
}

auto decode_token_unverified(std::string_view token) -> openclaw::Result<json> {
    try {
        auto decoded = jwt::decode<nl_traits>(std::string(token));

        json payload = decoded.get_payload_json();
        return payload;

    } catch (const std::exception& e) {
        LOG_ERROR("JWT decode error: {}", e.what());
        return std::unexpected(
            openclaw::make_error(ErrorCode::InvalidArgument,
                                "Failed to decode JWT", e.what()));
    }
}

} // namespace openclaw::infra
