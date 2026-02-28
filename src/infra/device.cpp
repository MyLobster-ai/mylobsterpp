#include "openclaw/infra/device.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <cstring>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/pem.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace openclaw::infra {

// ---------------------------------------------------------------------------
// Base64url encode/decode (RFC 4648 §5)
// ---------------------------------------------------------------------------

auto base64url_encode(std::string_view data) -> std::string {
    // Use standard base64 then convert
    auto b64 = utils::base64_encode(data);

    // Replace + with -, / with _, strip = padding
    for (auto& c : b64) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    // Strip trailing '='
    while (!b64.empty() && b64.back() == '=') {
        b64.pop_back();
    }
    return b64;
}

auto base64url_decode(std::string_view data) -> std::string {
    // Convert back to standard base64
    std::string b64(data);
    for (auto& c : b64) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    // Add padding
    while (b64.size() % 4 != 0) {
        b64 += '=';
    }
    return utils::base64_decode(b64);
}

// ---------------------------------------------------------------------------
// Ed25519 key generation
// ---------------------------------------------------------------------------

// The SPKI DER prefix for Ed25519 public keys (12 bytes):
// SEQUENCE { SEQUENCE { OID 1.3.101.112 } BIT STRING { ... } }
static constexpr unsigned char kEd25519SpkiPrefix[] = {
    0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65,
    0x70, 0x03, 0x21, 0x00
};
static constexpr size_t kEd25519SpkiPrefixLen = sizeof(kEd25519SpkiPrefix);
static constexpr size_t kEd25519RawPubKeyLen = 32;

auto generate_device_keypair() -> DeviceIdentity {
    DeviceIdentity identity;

    // Generate Ed25519 keypair
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (!ctx) {
        LOG_ERROR("Failed to create EVP_PKEY_CTX for Ed25519");
        return identity;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0 || EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        LOG_ERROR("Ed25519 key generation failed");
        EVP_PKEY_CTX_free(ctx);
        return identity;
    }
    EVP_PKEY_CTX_free(ctx);

    // Extract public key PEM
    {
        BIO* bio = BIO_new(BIO_s_mem());
        PEM_write_bio_PUBKEY(bio, pkey);
        char* pem_data = nullptr;
        long pem_len = BIO_get_mem_data(bio, &pem_data);
        identity.public_key_pem = std::string(pem_data, static_cast<size_t>(pem_len));
        BIO_free(bio);
    }

    // Extract private key PEM
    {
        BIO* bio = BIO_new(BIO_s_mem());
        PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
        char* pem_data = nullptr;
        long pem_len = BIO_get_mem_data(bio, &pem_data);
        identity.private_key_pem = std::string(pem_data, static_cast<size_t>(pem_len));
        BIO_free(bio);
    }

    // Extract raw 32-byte public key from SPKI DER
    {
        unsigned char* der = nullptr;
        int der_len = i2d_PUBKEY(pkey, &der);
        if (der_len > 0 && static_cast<size_t>(der_len) >= kEd25519SpkiPrefixLen + kEd25519RawPubKeyLen) {
            std::string_view raw_key(
                reinterpret_cast<const char*>(der + kEd25519SpkiPrefixLen),
                kEd25519RawPubKeyLen);
            identity.public_key_raw_b64url = base64url_encode(raw_key);
        }
        OPENSSL_free(der);
    }

    EVP_PKEY_free(pkey);

    // Derive device_id = SHA256 hex of raw public key
    auto raw_key = base64url_decode(identity.public_key_raw_b64url);
    identity.device_id = utils::sha256(raw_key);

    LOG_DEBUG("Generated Ed25519 device keypair, device_id={}", identity.device_id);

    return identity;
}

// ---------------------------------------------------------------------------
// Device ID derivation
// ---------------------------------------------------------------------------

auto derive_device_id_from_public_key(std::string_view pub_key_b64url) -> std::string {
    auto raw_key = base64url_decode(pub_key_b64url);
    return utils::sha256(raw_key);
}

// ---------------------------------------------------------------------------
// v2 payload construction
// ---------------------------------------------------------------------------

auto build_device_auth_payload(const DeviceAuthParams& params) -> std::string {
    // Format: "v2|deviceId|clientId|clientMode|role|scope1,scope2|signedAtMs|token|nonce"
    std::string payload = "v2|";
    payload += params.device_id + "|";
    payload += params.client_id + "|";
    payload += params.client_mode + "|";
    payload += params.role + "|";

    // Join scopes with comma
    for (size_t i = 0; i < params.scopes.size(); ++i) {
        if (i > 0) payload += ",";
        payload += params.scopes[i];
    }
    payload += "|";

    payload += std::to_string(params.signed_at_ms) + "|";
    payload += params.token + "|";
    payload += params.nonce;

    return payload;
}

// ---------------------------------------------------------------------------
// v3 payload construction (v2026.2.26)
// ---------------------------------------------------------------------------

auto normalize_device_metadata_for_auth(std::string_view value) -> std::string {
    std::string result;
    result.reserve(value.size());

    // Trim leading whitespace
    auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return {};
    auto end = value.find_last_not_of(" \t\r\n");
    auto trimmed = value.substr(start, end - start + 1);

    // ASCII-only lowercase, drop non-ASCII characters entirely
    for (char c : trimmed) {
        auto uc = static_cast<unsigned char>(c);
        if (uc > 127) {
            continue;  // drop non-ASCII
        }
        if (uc >= 'A' && uc <= 'Z') {
            result += static_cast<char>(uc + 32);  // lowercase
        } else {
            result += c;
        }
    }
    return result;
}

auto build_device_auth_payload_v3(const DeviceAuthV3Params& params) -> std::string {
    // Format: "v3|deviceId|clientId|clientMode|role|scope1,scope2|signedAtMs|token|nonce|platform|deviceFamily"
    std::string payload = "v3|";
    payload += params.device_id + "|";
    payload += params.client_id + "|";
    payload += params.client_mode + "|";
    payload += params.role + "|";

    for (size_t i = 0; i < params.scopes.size(); ++i) {
        if (i > 0) payload += ",";
        payload += params.scopes[i];
    }
    payload += "|";

    payload += std::to_string(params.signed_at_ms) + "|";
    payload += params.token + "|";
    payload += params.nonce + "|";
    payload += normalize_device_metadata_for_auth(params.platform) + "|";
    payload += normalize_device_metadata_for_auth(params.device_family);

    return payload;
}

// ---------------------------------------------------------------------------
// Ed25519 signature verification
// ---------------------------------------------------------------------------

auto verify_device_signature(std::string_view pub_key_b64url,
                             std::string_view payload,
                             std::string_view signature_b64url) -> bool {
    // Decode the raw public key
    auto raw_key = base64url_decode(pub_key_b64url);
    if (raw_key.size() != kEd25519RawPubKeyLen) {
        LOG_WARN("Invalid public key length: {} (expected {})",
                 raw_key.size(), kEd25519RawPubKeyLen);
        return false;
    }

    // Reconstruct SPKI DER from raw public key
    std::vector<unsigned char> spki_der(kEd25519SpkiPrefixLen + kEd25519RawPubKeyLen);
    std::memcpy(spki_der.data(), kEd25519SpkiPrefix, kEd25519SpkiPrefixLen);
    std::memcpy(spki_der.data() + kEd25519SpkiPrefixLen, raw_key.data(), kEd25519RawPubKeyLen);

    // Parse SPKI DER into EVP_PKEY
    const unsigned char* der_ptr = spki_der.data();
    EVP_PKEY* pkey = d2i_PUBKEY(nullptr, &der_ptr, static_cast<long>(spki_der.size()));
    if (!pkey) {
        LOG_WARN("Failed to parse Ed25519 public key from SPKI DER");
        return false;
    }

    // Decode signature
    auto sig = base64url_decode(signature_b64url);

    // Verify
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    bool valid = false;
    if (md_ctx) {
        if (EVP_DigestVerifyInit(md_ctx, nullptr, nullptr, nullptr, pkey) == 1) {
            int rc = EVP_DigestVerify(
                md_ctx,
                reinterpret_cast<const unsigned char*>(sig.data()), sig.size(),
                reinterpret_cast<const unsigned char*>(payload.data()), payload.size());
            valid = (rc == 1);
        }
        EVP_MD_CTX_free(md_ctx);
    }

    EVP_PKEY_free(pkey);
    return valid;
}

// ---------------------------------------------------------------------------
// Ed25519 signing
// ---------------------------------------------------------------------------

auto sign_device_payload(std::string_view private_key_pem,
                         std::string_view payload) -> std::string {
    // Parse PEM private key
    BIO* bio = BIO_new_mem_buf(private_key_pem.data(), static_cast<int>(private_key_pem.size()));
    if (!bio) {
        LOG_ERROR("Failed to create BIO for private key PEM");
        return {};
    }

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) {
        LOG_ERROR("Failed to parse Ed25519 private key from PEM");
        return {};
    }

    // Sign
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    std::string result;
    if (md_ctx) {
        if (EVP_DigestSignInit(md_ctx, nullptr, nullptr, nullptr, pkey) == 1) {
            size_t sig_len = 0;
            // First call to get signature length
            if (EVP_DigestSign(md_ctx, nullptr, &sig_len,
                               reinterpret_cast<const unsigned char*>(payload.data()),
                               payload.size()) == 1) {
                std::vector<unsigned char> sig(sig_len);
                if (EVP_DigestSign(md_ctx, sig.data(), &sig_len,
                                   reinterpret_cast<const unsigned char*>(payload.data()),
                                   payload.size()) == 1) {
                    result = base64url_encode(
                        std::string_view(reinterpret_cast<const char*>(sig.data()), sig_len));
                }
            }
        }
        EVP_MD_CTX_free(md_ctx);
    }

    EVP_PKEY_free(pkey);
    return result;
}

// ---------------------------------------------------------------------------
// System device identity detection
// ---------------------------------------------------------------------------

auto get_device_identity() -> openclaw::DeviceIdentity {
    DeviceIdentity identity;

#ifdef _WIN32
    // Get hostname
    char hostname_buf[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD hostname_len = sizeof(hostname_buf);
    if (::GetComputerNameA(hostname_buf, &hostname_len)) {
        identity.hostname = hostname_buf;
    } else {
        identity.hostname = "unknown";
        LOG_WARN("Failed to get hostname, using 'unknown'");
    }

    identity.os = "Windows";

    // Get architecture
    SYSTEM_INFO si;
    ::GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: identity.arch = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: identity.arch = "aarch64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: identity.arch = "x86"; break;
        default: identity.arch = "unknown"; break;
    }
#else
    // Get hostname
    char hostname_buf[256] = {};
    if (::gethostname(hostname_buf, sizeof(hostname_buf)) == 0) {
        identity.hostname = hostname_buf;
    } else {
        identity.hostname = "unknown";
        LOG_WARN("Failed to get hostname, using 'unknown'");
    }

    // Get OS and architecture via uname
    struct utsname uname_info {};
    if (::uname(&uname_info) == 0) {
        identity.os = uname_info.sysname;   // e.g. "Linux", "Darwin"
        identity.arch = uname_info.machine;  // e.g. "x86_64", "aarch64", "arm64"
    } else {
        identity.os = "unknown";
        identity.arch = "unknown";
        LOG_WARN("Failed to get uname info");
    }
#endif

    // Generate a stable device ID by hashing hostname + os + arch
    auto raw = identity.hostname + ":" + identity.os + ":" + identity.arch;
    identity.device_id = openclaw::utils::sha256(raw).substr(0, 16);

    LOG_DEBUG("Device identity: id={}, host={}, os={}, arch={}",
              identity.device_id, identity.hostname,
              identity.os, identity.arch);

    return identity;
}

} // namespace openclaw::infra
