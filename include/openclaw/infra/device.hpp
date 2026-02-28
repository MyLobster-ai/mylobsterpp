#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "openclaw/core/types.hpp"

namespace openclaw::infra {

/// Detects and returns the current device identity.
/// Populates hostname, OS name, architecture, and a stable device ID
/// derived from hashing the hostname + OS + arch combination.
auto get_device_identity() -> openclaw::DeviceIdentity;

/// Parameters for constructing a v2 device auth payload.
struct DeviceAuthParams {
    std::string device_id;
    std::string client_id;
    std::string client_mode;
    std::string role;
    std::vector<std::string> scopes;
    int64_t signed_at_ms;
    std::string token;
    std::string nonce;
};

/// v2026.2.26: Parameters for constructing a v3 device auth payload.
/// Extends v2 with platform and device family metadata.
struct DeviceAuthV3Params : DeviceAuthParams {
    std::string platform;       // "darwin", "linux", "windows"
    std::string device_family;  // "desktop", "mobile", "server"
};

/// Generate an Ed25519 keypair and return a DeviceIdentity with crypto fields
/// populated (public_key_pem, private_key_pem, public_key_raw_b64url, device_id).
auto generate_device_keypair() -> openclaw::DeviceIdentity;

/// Derive a device ID from a base64url-encoded raw public key.
/// Returns SHA256 hex of the 32-byte raw public key.
auto derive_device_id_from_public_key(std::string_view pub_key_b64url) -> std::string;

/// Build the v2 pipe-delimited payload string for device auth signing.
/// Format: "v2|deviceId|clientId|clientMode|role|scope1,scope2|signedAtMs|token|nonce"
auto build_device_auth_payload(const DeviceAuthParams& params) -> std::string;

/// v2026.2.26: Build the v3 pipe-delimited payload string for device auth signing.
/// Format: "v3|deviceId|clientId|clientMode|role|scope1,scope2|signedAtMs|token|nonce|platform|deviceFamily"
auto build_device_auth_payload_v3(const DeviceAuthV3Params& params) -> std::string;

/// v2026.2.26: Normalize device metadata for auth payload construction.
/// Trims whitespace, converts to ASCII lowercase, drops non-ASCII characters.
auto normalize_device_metadata_for_auth(std::string_view value) -> std::string;

/// Verify an Ed25519 signature over a payload using a base64url-encoded raw public key.
auto verify_device_signature(std::string_view pub_key_b64url,
                             std::string_view payload,
                             std::string_view signature_b64url) -> bool;

/// Sign a payload with an Ed25519 private key (PEM format).
/// Returns the signature as base64url-encoded string.
auto sign_device_payload(std::string_view private_key_pem,
                         std::string_view payload) -> std::string;

/// Base64url encode (RFC 4648 §5): +→-, /→_, no padding.
auto base64url_encode(std::string_view data) -> std::string;

/// Base64url decode (RFC 4648 §5): -→+, _→/, adds padding.
auto base64url_decode(std::string_view data) -> std::string;

} // namespace openclaw::infra
