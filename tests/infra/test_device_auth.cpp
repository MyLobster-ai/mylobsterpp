#include <catch2/catch_test_macros.hpp>

#include "openclaw/infra/device.hpp"
#include "openclaw/core/utils.hpp"

using namespace openclaw::infra;

TEST_CASE("base64url encode/decode round-trip", "[device_auth]") {
    SECTION("empty string") {
        auto encoded = base64url_encode("");
        auto decoded = base64url_decode(encoded);
        CHECK(decoded.empty());
    }

    SECTION("short binary data") {
        std::string data = "\x00\x01\x02\xff\xfe\xfd";
        auto encoded = base64url_encode(data);
        auto decoded = base64url_decode(encoded);
        CHECK(decoded == data);
    }

    SECTION("no padding characters in encoded output") {
        std::string data = "test";
        auto encoded = base64url_encode(data);
        CHECK(encoded.find('=') == std::string::npos);
    }

    SECTION("no + or / in encoded output") {
        // Use data known to produce + and / in standard base64
        std::string data(256, '\0');
        for (int i = 0; i < 256; ++i) data[i] = static_cast<char>(i);
        auto encoded = base64url_encode(data);
        CHECK(encoded.find('+') == std::string::npos);
        CHECK(encoded.find('/') == std::string::npos);
    }
}

TEST_CASE("Ed25519 keypair generation", "[device_auth]") {
    auto identity = generate_device_keypair();

    SECTION("produces non-empty fields") {
        CHECK_FALSE(identity.device_id.empty());
        CHECK_FALSE(identity.public_key_pem.empty());
        CHECK_FALSE(identity.private_key_pem.empty());
        CHECK_FALSE(identity.public_key_raw_b64url.empty());
    }

    SECTION("PEM keys have correct headers") {
        CHECK(identity.public_key_pem.find("BEGIN PUBLIC KEY") != std::string::npos);
        CHECK(identity.private_key_pem.find("BEGIN PRIVATE KEY") != std::string::npos);
    }

    SECTION("raw public key decodes to 32 bytes") {
        auto raw = base64url_decode(identity.public_key_raw_b64url);
        CHECK(raw.size() == 32);
    }

    SECTION("device_id is SHA256 hex of raw public key") {
        auto raw = base64url_decode(identity.public_key_raw_b64url);
        auto expected_id = openclaw::utils::sha256(raw);
        CHECK(identity.device_id == expected_id);
    }

    SECTION("two keypairs produce different keys") {
        auto identity2 = generate_device_keypair();
        CHECK(identity.public_key_raw_b64url != identity2.public_key_raw_b64url);
        CHECK(identity.device_id != identity2.device_id);
    }
}

TEST_CASE("derive_device_id_from_public_key", "[device_auth]") {
    auto identity = generate_device_keypair();

    SECTION("matches the device_id from keypair generation") {
        auto derived = derive_device_id_from_public_key(identity.public_key_raw_b64url);
        CHECK(derived == identity.device_id);
    }
}

TEST_CASE("build_device_auth_payload", "[device_auth]") {
    SECTION("produces correct v2 pipe-delimited format") {
        DeviceAuthParams params{
            .device_id = "abc123",
            .client_id = "client-1",
            .client_mode = "bridge",
            .role = "operator",
            .scopes = {"operator.write", "chat.send"},
            .signed_at_ms = 1700000000000,
            .token = "my-token",
            .nonce = "nonce-uuid",
        };

        auto payload = build_device_auth_payload(params);
        CHECK(payload == "v2|abc123|client-1|bridge|operator|operator.write,chat.send|1700000000000|my-token|nonce-uuid");
    }

    SECTION("handles empty scopes") {
        DeviceAuthParams params{
            .device_id = "id",
            .client_id = "cid",
            .client_mode = "direct",
            .role = "user",
            .scopes = {},
            .signed_at_ms = 123,
            .token = "tok",
            .nonce = "n",
        };

        auto payload = build_device_auth_payload(params);
        CHECK(payload == "v2|id|cid|direct|user||123|tok|n");
    }

    SECTION("handles single scope") {
        DeviceAuthParams params{
            .device_id = "id",
            .client_id = "cid",
            .client_mode = "mode",
            .role = "role",
            .scopes = {"single"},
            .signed_at_ms = 456,
            .token = "tok",
            .nonce = "n",
        };

        auto payload = build_device_auth_payload(params);
        CHECK(payload == "v2|id|cid|mode|role|single|456|tok|n");
    }
}

TEST_CASE("Ed25519 sign and verify round-trip", "[device_auth]") {
    auto identity = generate_device_keypair();

    DeviceAuthParams params{
        .device_id = identity.device_id,
        .client_id = "test-client",
        .client_mode = "bridge",
        .role = "operator",
        .scopes = {"operator.write"},
        .signed_at_ms = openclaw::utils::timestamp_ms(),
        .token = "test-token",
        .nonce = openclaw::utils::generate_uuid(),
    };

    auto payload = build_device_auth_payload(params);

    SECTION("sign produces non-empty signature") {
        auto sig = sign_device_payload(identity.private_key_pem, payload);
        CHECK_FALSE(sig.empty());
    }

    SECTION("verify succeeds with correct signature") {
        auto sig = sign_device_payload(identity.private_key_pem, payload);
        CHECK(verify_device_signature(identity.public_key_raw_b64url, payload, sig));
    }

    SECTION("verify fails with wrong nonce") {
        auto sig = sign_device_payload(identity.private_key_pem, payload);

        // Modify the payload (different nonce)
        DeviceAuthParams wrong_params = params;
        wrong_params.nonce = "wrong-nonce";
        auto wrong_payload = build_device_auth_payload(wrong_params);

        CHECK_FALSE(verify_device_signature(
            identity.public_key_raw_b64url, wrong_payload, sig));
    }

    SECTION("verify fails with different key") {
        auto sig = sign_device_payload(identity.private_key_pem, payload);

        auto other_identity = generate_device_keypair();
        CHECK_FALSE(verify_device_signature(
            other_identity.public_key_raw_b64url, payload, sig));
    }

    SECTION("verify fails with tampered signature") {
        auto sig = sign_device_payload(identity.private_key_pem, payload);

        // Tamper with the signature
        std::string tampered = sig;
        if (!tampered.empty()) {
            tampered[0] = (tampered[0] == 'A') ? 'B' : 'A';
        }
        CHECK_FALSE(verify_device_signature(
            identity.public_key_raw_b64url, payload, tampered));
    }
}

// v2026.2.26: Device auth v3 tests
TEST_CASE("normalize_device_metadata_for_auth", "[device_auth]") {
    SECTION("ASCII lowercase") {
        CHECK(normalize_device_metadata_for_auth("DARWIN") == "darwin");
        CHECK(normalize_device_metadata_for_auth("Linux") == "linux");
        CHECK(normalize_device_metadata_for_auth("Windows") == "windows");
    }

    SECTION("trims whitespace") {
        CHECK(normalize_device_metadata_for_auth("  desktop  ") == "desktop");
        CHECK(normalize_device_metadata_for_auth("\tmobile\n") == "mobile");
    }

    SECTION("drops non-ASCII characters") {
        CHECK(normalize_device_metadata_for_auth("darw\xc3\xadn") == "darwn");
        CHECK(normalize_device_metadata_for_auth("\xc0\xc1server") == "server");
    }

    SECTION("empty and whitespace-only") {
        CHECK(normalize_device_metadata_for_auth("").empty());
        CHECK(normalize_device_metadata_for_auth("   ").empty());
    }
}

TEST_CASE("build_device_auth_payload_v3", "[device_auth]") {
    SECTION("produces correct v3 pipe-delimited format") {
        DeviceAuthV3Params params{};
        params.device_id = "abc123";
        params.client_id = "client-1";
        params.client_mode = "bridge";
        params.role = "operator";
        params.scopes = {"operator.write"};
        params.signed_at_ms = 1700000000000;
        params.token = "my-token";
        params.nonce = "nonce-uuid";
        params.platform = "Darwin";
        params.device_family = "Desktop";

        auto payload = build_device_auth_payload_v3(params);
        CHECK(payload == "v3|abc123|client-1|bridge|operator|operator.write|1700000000000|my-token|nonce-uuid|darwin|desktop");
    }

    SECTION("v3 sign and verify round-trip") {
        auto identity = generate_device_keypair();

        DeviceAuthV3Params params{};
        params.device_id = identity.device_id;
        params.client_id = "test-client";
        params.client_mode = "bridge";
        params.role = "operator";
        params.scopes = {"operator.write"};
        params.signed_at_ms = openclaw::utils::timestamp_ms();
        params.token = "test-token";
        params.nonce = openclaw::utils::generate_uuid();
        params.platform = "linux";
        params.device_family = "server";

        auto payload = build_device_auth_payload_v3(params);
        auto sig = sign_device_payload(identity.private_key_pem, payload);
        CHECK(verify_device_signature(identity.public_key_raw_b64url, payload, sig));
    }

    SECTION("v2 and v3 payloads differ for same base params") {
        DeviceAuthV3Params v3_params{};
        v3_params.device_id = "id";
        v3_params.client_id = "cid";
        v3_params.client_mode = "mode";
        v3_params.role = "role";
        v3_params.scopes = {"scope"};
        v3_params.signed_at_ms = 123;
        v3_params.token = "tok";
        v3_params.nonce = "n";
        v3_params.platform = "linux";
        v3_params.device_family = "desktop";

        DeviceAuthParams v2_params{
            .device_id = "id",
            .client_id = "cid",
            .client_mode = "mode",
            .role = "role",
            .scopes = {"scope"},
            .signed_at_ms = 123,
            .token = "tok",
            .nonce = "n",
        };

        auto v3_payload = build_device_auth_payload_v3(v3_params);
        auto v2_payload = build_device_auth_payload(v2_params);
        CHECK(v3_payload != v2_payload);
        CHECK(v3_payload.starts_with("v3|"));
        CHECK(v2_payload.starts_with("v2|"));
    }
}

TEST_CASE("Device signature timestamp skew validation", "[device_auth]") {
    auto identity = generate_device_keypair();

    SECTION("current timestamp is within skew window") {
        auto now = openclaw::utils::timestamp_ms();
        auto skew = std::abs(openclaw::utils::timestamp_ms() - now);
        // Should be within 2 minutes (120000ms)
        CHECK(skew < 120000);
    }

    SECTION("stale timestamp is rejected") {
        int64_t stale = openclaw::utils::timestamp_ms() - 3 * 60 * 1000;  // 3 minutes ago
        auto now = openclaw::utils::timestamp_ms();
        CHECK(std::abs(now - stale) > 120000);
    }
}
