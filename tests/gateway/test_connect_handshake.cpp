#include <catch2/catch_test_macros.hpp>

#include "openclaw/gateway/frame.hpp"
#include "openclaw/gateway/server.hpp"
#include "openclaw/infra/device.hpp"
#include "openclaw/core/utils.hpp"

using namespace openclaw::gateway;
using namespace openclaw::infra;
using json = nlohmann::json;

TEST_CASE("Connect challenge event format", "[connect_handshake]") {
    auto nonce = openclaw::utils::generate_uuid();
    auto ts = openclaw::utils::timestamp_ms();

    auto challenge = make_event("connect.challenge", json{
        {"nonce", nonce},
        {"ts", ts},
    });

    SECTION("event name is connect.challenge") {
        CHECK(challenge.event == "connect.challenge");
    }

    SECTION("payload contains nonce and ts") {
        CHECK(challenge.data["nonce"] == nonce);
        CHECK(challenge.data["ts"] == ts);
    }

    SECTION("serialized frame has correct type") {
        json j;
        to_json(j, challenge);
        CHECK(j["type"] == "event");
        CHECK(j["event"] == "connect.challenge");
        CHECK(j["payload"]["nonce"] == nonce);
    }
}

TEST_CASE("Connect request frame format", "[connect_handshake]") {
    auto identity = generate_device_keypair();
    std::string nonce = openclaw::utils::generate_uuid();
    std::string token = "test-auth-token";
    auto signed_at = openclaw::utils::timestamp_ms();

    DeviceAuthParams params{
        .device_id = identity.device_id,
        .client_id = "mylobster-bridge",
        .client_mode = "bridge",
        .role = "operator",
        .scopes = {"operator.write", "chat.send"},
        .signed_at_ms = signed_at,
        .token = token,
        .nonce = nonce,
    };

    auto payload_str = build_device_auth_payload(params);
    auto signature = sign_device_payload(identity.private_key_pem, payload_str);

    json connect_params = {
        {"minProtocol", 3},
        {"maxProtocol", 3},
        {"client", "mylobster-bridge/1.0"},
        {"role", "operator"},
        {"scopes", {"operator.write", "chat.send"}},
        {"auth", {{"token", token}}},
        {"device", {
            {"id", identity.device_id},
            {"publicKey", identity.public_key_raw_b64url},
            {"signedAt", signed_at},
            {"nonce", nonce},
            {"signature", signature},
            {"clientId", "mylobster-bridge"},
            {"clientMode", "bridge"},
        }},
    };

    SECTION("request is a valid frame") {
        RequestFrame req{
            .id = "connect-1",
            .method = "connect",
            .params = connect_params,
        };

        auto serialized = serialize_frame(Frame{req});
        auto parsed = parse_frame(serialized);

        REQUIRE(parsed.has_value());
        REQUIRE(std::holds_alternative<RequestFrame>(*parsed));
        auto& restored = std::get<RequestFrame>(*parsed);
        CHECK(restored.method == "connect");
        CHECK(restored.params["minProtocol"] == 3);
    }

    SECTION("device signature can be verified from connect params") {
        // Reconstruct and verify
        auto dev = connect_params["device"];
        auto derived_id = derive_device_id_from_public_key(
            dev["publicKey"].get<std::string>());
        CHECK(derived_id == identity.device_id);

        auto verified = verify_device_signature(
            dev["publicKey"].get<std::string>(),
            payload_str,
            dev["signature"].get<std::string>());
        CHECK(verified);
    }
}

TEST_CASE("Hello-ok response format", "[connect_handshake]") {
    auto resp = make_response("connect-1", json{
        {"type", "hello-ok"},
        {"protocol", GatewayServer::PROTOCOL_VERSION},
        {"policy", {
            {"tickIntervalMs", 15000},
        }},
    });

    SECTION("response is a success with ok=true") {
        CHECK(resp.ok == true);
        CHECK_FALSE(resp.is_error());
    }

    SECTION("result contains hello-ok type and protocol version") {
        REQUIRE(resp.result.has_value());
        CHECK((*resp.result)["type"] == "hello-ok");
        CHECK((*resp.result)["protocol"] == 3);
    }

    SECTION("result contains tick interval policy") {
        REQUIRE(resp.result.has_value());
        CHECK((*resp.result)["policy"]["tickIntervalMs"] == 15000);
    }

    SECTION("serialized format matches OpenClaw v2026.2.22") {
        json j;
        to_json(j, resp);

        CHECK(j["type"] == "res");
        CHECK(j["ok"] == true);
        CHECK(j["id"] == "connect-1");
        CHECK(j["result"]["type"] == "hello-ok");
        CHECK(j["result"]["protocol"] == 3);
    }
}

TEST_CASE("Protocol version constants", "[connect_handshake]") {
    CHECK(GatewayServer::PROTOCOL_VERSION == 3);
    CHECK(GatewayServer::DEVICE_SIGNATURE_SKEW_MS == 120000);
}

TEST_CASE("End-to-end connect handshake payload construction", "[connect_handshake]") {
    // Simulate the full handshake flow without WebSocket transport

    // 1. Server generates challenge
    auto challenge_nonce = openclaw::utils::generate_uuid();

    // 2. Client generates keypair
    auto identity = generate_device_keypair();

    // 3. Client builds connect params with device auth
    std::string token = "jwt-token-here";
    auto signed_at = openclaw::utils::timestamp_ms();

    DeviceAuthParams auth_params{
        .device_id = identity.device_id,
        .client_id = "bridge-v1",
        .client_mode = "bridge",
        .role = "operator",
        .scopes = {"operator.write"},
        .signed_at_ms = signed_at,
        .token = token,
        .nonce = challenge_nonce,
    };

    auto payload = build_device_auth_payload(auth_params);
    auto signature = sign_device_payload(identity.private_key_pem, payload);

    // 4. Server validates device identity
    // 4a: Derive device ID from public key
    auto derived_id = derive_device_id_from_public_key(identity.public_key_raw_b64url);
    CHECK(derived_id == identity.device_id);

    // 4b: Check timestamp skew
    auto now = openclaw::utils::timestamp_ms();
    CHECK(std::abs(now - signed_at) <= GatewayServer::DEVICE_SIGNATURE_SKEW_MS);

    // 4c: Nonce matches challenge
    CHECK(auth_params.nonce == challenge_nonce);

    // 4d: Verify signature
    CHECK(verify_device_signature(identity.public_key_raw_b64url, payload, signature));
}
