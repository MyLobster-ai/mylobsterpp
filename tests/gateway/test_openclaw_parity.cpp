/// Parity tests verifying MyLobsterPP produces frames and structures
/// byte-for-byte compatible with OpenClaw v2026.2.24 (TypeScript reference).
///
/// Each test references the exact OpenClaw source file and line(s) it
/// validates against, so regressions can be traced back to the spec.
///
/// v2026.2.23: Browser SSRF policy default, Kilo Gateway provider,
///             Vercel AI Gateway normalization.
/// v2026.2.24: Heartbeat DM blocking, Docker sandbox namespace-join,
///             tools.catalog RPC, cron paging, Talk config, HSTS headers,
///             bootstrap caching, security audit heuristics.

#include <catch2/catch_test_macros.hpp>

#include "openclaw/core/error.hpp"
#include "openclaw/core/utils.hpp"
#include "openclaw/gateway/frame.hpp"
#include "openclaw/gateway/server.hpp"
#include "openclaw/infra/device.hpp"

using namespace openclaw::gateway;
using namespace openclaw::infra;
using json = nlohmann::json;

// ==========================================================================
// Frame type string parity
// Ref: openclaw/src/gateway/protocol/schema/frames.ts
// ==========================================================================

TEST_CASE("Request frame type string is 'req'", "[parity][frame]") {
    RequestFrame req{.id = "r1", .method = "chat.send", .params = json::object()};
    json j;
    to_json(j, req);
    CHECK(j["type"] == "req");
}

TEST_CASE("Response frame type string is 'res'", "[parity][frame]") {
    auto resp = make_response("r1", json{{"status", "ok"}});
    json j;
    to_json(j, resp);
    CHECK(j["type"] == "res");
}

TEST_CASE("Event frame type string is 'event'", "[parity][frame]") {
    auto evt = make_event("connect.challenge", json{{"nonce", "abc"}});
    json j;
    to_json(j, evt);
    CHECK(j["type"] == "event");
}

// ==========================================================================
// ResponseFrame payload field parity
// Ref: openclaw/src/gateway/protocol/schema/frames.ts — HelloOkSchema
// OpenClaw sends the result under "payload", NOT "result"
// ==========================================================================

TEST_CASE("ResponseFrame serializes result as 'payload' field", "[parity][frame]") {
    auto resp = make_response("r1", json{{"value", 42}});
    json j;
    to_json(j, resp);

    SECTION("'payload' key exists") {
        REQUIRE(j.contains("payload"));
        CHECK(j["payload"]["value"] == 42);
    }

    SECTION("'result' key does NOT exist") {
        CHECK_FALSE(j.contains("result"));
    }
}

TEST_CASE("ResponseFrame deserializes 'payload' field into result", "[parity][frame]") {
    json j = {
        {"id", "r1"},
        {"ok", true},
        {"payload", {{"value", 42}}},
    };

    ResponseFrame resp;
    from_json(j, resp);

    REQUIRE(resp.result.has_value());
    CHECK((*resp.result)["value"] == 42);
}

TEST_CASE("ResponseFrame backwards-compat: accepts 'result' field", "[parity][frame]") {
    json j = {
        {"id", "r1"},
        {"ok", true},
        {"result", {{"value", 99}}},
    };

    ResponseFrame resp;
    from_json(j, resp);

    REQUIRE(resp.result.has_value());
    CHECK((*resp.result)["value"] == 99);
}

// ==========================================================================
// ResponseFrame ok field parity
// Ref: openclaw/src/gateway/protocol/schema/frames.ts — HelloOkSchema
// ==========================================================================

TEST_CASE("Success response has ok=true", "[parity][frame]") {
    auto resp = make_response("r1", json::object());
    json j;
    to_json(j, resp);
    CHECK(j.contains("ok"));
    CHECK(j["ok"] == true);
}

TEST_CASE("Error response has ok=false", "[parity][frame]") {
    auto resp = make_error_response("r1", openclaw::ErrorCode::NotFound, "not found");
    json j;
    to_json(j, resp);
    CHECK(j.contains("ok"));
    CHECK(j["ok"] == false);
}

// ==========================================================================
// Error code string parity
// Ref: openclaw/src/gateway/protocol/schema/frames.ts:114-120
//   ErrorShapeSchema.code = NonEmptyString (not integer)
// ==========================================================================

TEST_CASE("Error code is a string, not integer", "[parity][frame]") {
    auto resp = make_error_response("r1", openclaw::ErrorCode::NotFound, "not found");
    json j;
    to_json(j, resp);

    REQUIRE(j.contains("error"));
    REQUIRE(j["error"].contains("code"));
    CHECK(j["error"]["code"].is_string());
    CHECK(j["error"]["code"] == "NOT_FOUND");
}

TEST_CASE("All error codes map to uppercase snake_case strings", "[parity][frame]") {
    struct TestCase {
        openclaw::ErrorCode code;
        std::string expected;
    };

    std::vector<TestCase> cases = {
        {openclaw::ErrorCode::Unknown, "UNKNOWN"},
        {openclaw::ErrorCode::InvalidConfig, "INVALID_CONFIG"},
        {openclaw::ErrorCode::InvalidArgument, "INVALID_ARGUMENT"},
        {openclaw::ErrorCode::NotFound, "NOT_FOUND"},
        {openclaw::ErrorCode::AlreadyExists, "ALREADY_EXISTS"},
        {openclaw::ErrorCode::Unauthorized, "UNAUTHORIZED"},
        {openclaw::ErrorCode::Forbidden, "FORBIDDEN"},
        {openclaw::ErrorCode::Timeout, "TIMEOUT"},
        {openclaw::ErrorCode::ConnectionFailed, "CONNECTION_FAILED"},
        {openclaw::ErrorCode::ConnectionClosed, "CONNECTION_CLOSED"},
        {openclaw::ErrorCode::ProtocolError, "PROTOCOL_ERROR"},
        {openclaw::ErrorCode::SerializationError, "SERIALIZATION_ERROR"},
        {openclaw::ErrorCode::IoError, "IO_ERROR"},
        {openclaw::ErrorCode::DatabaseError, "DATABASE_ERROR"},
        {openclaw::ErrorCode::ProviderError, "PROVIDER_ERROR"},
        {openclaw::ErrorCode::ChannelError, "CHANNEL_ERROR"},
        {openclaw::ErrorCode::PluginError, "PLUGIN_ERROR"},
        {openclaw::ErrorCode::BrowserError, "BROWSER_ERROR"},
        {openclaw::ErrorCode::MemoryError, "MEMORY_ERROR"},
        {openclaw::ErrorCode::SessionError, "SESSION_ERROR"},
        {openclaw::ErrorCode::RateLimited, "RATE_LIMITED"},
        {openclaw::ErrorCode::InternalError, "INTERNAL_ERROR"},
    };

    for (const auto& tc : cases) {
        INFO("ErrorCode::" << tc.expected);
        auto resp = make_error_response("e", tc.code, "msg");
        json j;
        to_json(j, resp);
        CHECK(j["error"]["code"] == tc.expected);
    }
}

// ==========================================================================
// EventFrame payload field parity
// Ref: openclaw uses "payload" for event data, not "data"
// ==========================================================================

TEST_CASE("EventFrame serializes data as 'payload' field", "[parity][frame]") {
    auto evt = make_event("status.update", json{{"online", true}});
    json j;
    to_json(j, evt);

    CHECK(j.contains("payload"));
    CHECK_FALSE(j.contains("data"));
    CHECK(j["payload"]["online"] == true);
}

TEST_CASE("EventFrame backwards-compat: accepts 'data' field", "[parity][frame]") {
    json j = {
        {"event", "legacy.evt"},
        {"data", {{"text", "old"}}},
    };

    EventFrame evt;
    from_json(j, evt);
    CHECK(evt.data["text"] == "old");
}

// ==========================================================================
// Server constants parity
// Ref: openclaw/src/gateway/server-constants.ts
// ==========================================================================

TEST_CASE("PROTOCOL_VERSION matches OpenClaw", "[parity][constants]") {
    CHECK(GatewayServer::PROTOCOL_VERSION == 3);
}

TEST_CASE("TICK_INTERVAL_MS matches OpenClaw (30000)", "[parity][constants]") {
    CHECK(GatewayServer::TICK_INTERVAL_MS == 30000);
}

TEST_CASE("MAX_PAYLOAD_BYTES matches OpenClaw (25 MB)", "[parity][constants]") {
    CHECK(GatewayServer::MAX_PAYLOAD_BYTES == 25 * 1024 * 1024);
}

TEST_CASE("MAX_BUFFERED_BYTES matches OpenClaw (50 MB)", "[parity][constants]") {
    CHECK(GatewayServer::MAX_BUFFERED_BYTES == 50 * 1024 * 1024);
}

TEST_CASE("DEVICE_SIGNATURE_SKEW_MS matches OpenClaw (120000)", "[parity][constants]") {
    CHECK(GatewayServer::DEVICE_SIGNATURE_SKEW_MS == 120000);
}

// ==========================================================================
// Hello-ok policy field parity
// Ref: openclaw/src/gateway/server/ws-connection/message-handler.ts:805-809
//   policy: { maxPayload, maxBufferedBytes, tickIntervalMs }
// ==========================================================================

TEST_CASE("Hello-ok policy has correct field names and values", "[parity][handshake]") {
    // Simulate what handle_connection() would send
    auto hello_ok = make_response("connect-1", json{
        {"type", "hello-ok"},
        {"protocol", GatewayServer::PROTOCOL_VERSION},
        {"policy", {
            {"tickIntervalMs", GatewayServer::TICK_INTERVAL_MS},
            {"maxPayload", GatewayServer::MAX_PAYLOAD_BYTES},
            {"maxBufferedBytes", GatewayServer::MAX_BUFFERED_BYTES},
        }},
    });

    REQUIRE(hello_ok.result.has_value());
    auto& policy = (*hello_ok.result)["policy"];

    SECTION("tickIntervalMs = 30000") {
        CHECK(policy["tickIntervalMs"] == 30000);
    }

    SECTION("maxPayload = 25 * 1024 * 1024 (not maxPayloadBytes)") {
        CHECK(policy.contains("maxPayload"));
        CHECK_FALSE(policy.contains("maxPayloadBytes"));
        CHECK(policy["maxPayload"] == 25 * 1024 * 1024);
    }

    SECTION("maxBufferedBytes = 50 * 1024 * 1024") {
        CHECK(policy["maxBufferedBytes"] == 50 * 1024 * 1024);
    }
}

TEST_CASE("Hello-ok serialized matches OpenClaw wire format", "[parity][handshake]") {
    auto hello_ok = make_response("c1", json{
        {"type", "hello-ok"},
        {"protocol", 3},
        {"policy", {
            {"tickIntervalMs", 30000},
            {"maxPayload", 25 * 1024 * 1024},
            {"maxBufferedBytes", 50 * 1024 * 1024},
        }},
    });

    json j;
    to_json(j, hello_ok);

    // Wire format must have: type=res, ok=true, payload (not result)
    CHECK(j["type"] == "res");
    CHECK(j["ok"] == true);
    CHECK(j.contains("payload"));
    CHECK_FALSE(j.contains("result"));
    CHECK(j["payload"]["type"] == "hello-ok");
    CHECK(j["payload"]["protocol"] == 3);
    CHECK(j["payload"]["policy"]["tickIntervalMs"] == 30000);
    CHECK(j["payload"]["policy"]["maxPayload"] == 25 * 1024 * 1024);
    CHECK(j["payload"]["policy"]["maxBufferedBytes"] == 50 * 1024 * 1024);
}

// ==========================================================================
// Connect challenge event parity
// Ref: openclaw/src/gateway/server/ws-connection/message-handler.ts
//   Sends: { type: "event", event: "connect.challenge", payload: { nonce, ts } }
// ==========================================================================

TEST_CASE("Connect challenge event wire format", "[parity][handshake]") {
    auto nonce = openclaw::utils::generate_uuid();
    auto ts = openclaw::utils::timestamp_ms();

    auto challenge = make_event("connect.challenge", json{
        {"nonce", nonce},
        {"ts", ts},
    });

    json j;
    to_json(j, challenge);

    CHECK(j["type"] == "event");
    CHECK(j["event"] == "connect.challenge");
    CHECK(j.contains("payload"));
    CHECK_FALSE(j.contains("data"));
    CHECK(j["payload"]["nonce"] == nonce);
    CHECK(j["payload"]["ts"] == ts);
}

// ==========================================================================
// Device auth v2 payload format parity
// Ref: openclaw/src/gateway/device-auth.ts:15-16
//   "v2"|deviceId|clientId|clientMode|role|scopes|signedAtMs|token|nonce
// ==========================================================================

TEST_CASE("Device auth v2 payload format matches OpenClaw", "[parity][device]") {
    DeviceAuthParams params{
        .device_id = "dev123",
        .client_id = "bridge-v1",
        .client_mode = "bridge",
        .role = "operator",
        .scopes = {"operator.write", "chat.send"},
        .signed_at_ms = 1700000000000,
        .token = "jwt-token",
        .nonce = "challenge-nonce",
    };

    auto payload = build_device_auth_payload(params);

    // Must be pipe-delimited v2 format
    CHECK(payload == "v2|dev123|bridge-v1|bridge|operator|operator.write,chat.send|1700000000000|jwt-token|challenge-nonce");
}

TEST_CASE("Device auth payload: scopes comma-separated", "[parity][device]") {
    DeviceAuthParams params{
        .device_id = "d",
        .client_id = "c",
        .client_mode = "m",
        .role = "r",
        .scopes = {"a", "b", "c"},
        .signed_at_ms = 1,
        .token = "t",
        .nonce = "n",
    };

    auto payload = build_device_auth_payload(params);
    CHECK(payload == "v2|d|c|m|r|a,b,c|1|t|n");
}

TEST_CASE("Device auth payload: empty scopes produce empty field", "[parity][device]") {
    DeviceAuthParams params{
        .device_id = "d",
        .client_id = "c",
        .client_mode = "m",
        .role = "r",
        .scopes = {},
        .signed_at_ms = 1,
        .token = "t",
        .nonce = "n",
    };

    auto payload = build_device_auth_payload(params);
    CHECK(payload == "v2|d|c|m|r||1|t|n");
}

// ==========================================================================
// Device identity: Ed25519 keypair + ID derivation parity
// Ref: openclaw/src/gateway/device-auth.ts
//   device_id = SHA256(hex) of raw 32-byte Ed25519 public key
// ==========================================================================

TEST_CASE("Device ID is SHA256 hex of 32-byte raw public key", "[parity][device]") {
    auto identity = generate_device_keypair();

    // Raw public key should decode to exactly 32 bytes
    auto raw = base64url_decode(identity.public_key_raw_b64url);
    CHECK(raw.size() == 32);

    // Device ID should be SHA256 hex of raw public key
    auto expected_id = openclaw::utils::sha256(raw);
    CHECK(identity.device_id == expected_id);
}

TEST_CASE("derive_device_id_from_public_key matches generated ID", "[parity][device]") {
    auto identity = generate_device_keypair();
    auto derived = derive_device_id_from_public_key(identity.public_key_raw_b64url);
    CHECK(derived == identity.device_id);
}

// ==========================================================================
// Ed25519 signature round-trip parity
// Ref: openclaw/src/gateway/device-auth.ts — sign + verify
// ==========================================================================

TEST_CASE("Ed25519 sign/verify round-trip with v2 payload", "[parity][device]") {
    auto identity = generate_device_keypair();
    auto nonce = openclaw::utils::generate_uuid();

    DeviceAuthParams params{
        .device_id = identity.device_id,
        .client_id = "test-client",
        .client_mode = "bridge",
        .role = "operator",
        .scopes = {"operator.write"},
        .signed_at_ms = openclaw::utils::timestamp_ms(),
        .token = "test-token",
        .nonce = nonce,
    };

    auto payload = build_device_auth_payload(params);
    auto signature = sign_device_payload(identity.private_key_pem, payload);

    CHECK_FALSE(signature.empty());
    CHECK(verify_device_signature(identity.public_key_raw_b64url, payload, signature));
}

TEST_CASE("Signature verification fails with wrong nonce", "[parity][device]") {
    auto identity = generate_device_keypair();

    DeviceAuthParams params{
        .device_id = identity.device_id,
        .client_id = "c",
        .client_mode = "bridge",
        .role = "operator",
        .scopes = {"operator.write"},
        .signed_at_ms = openclaw::utils::timestamp_ms(),
        .token = "t",
        .nonce = "correct-nonce",
    };

    auto payload = build_device_auth_payload(params);
    auto signature = sign_device_payload(identity.private_key_pem, payload);

    // Tamper with the nonce
    params.nonce = "wrong-nonce";
    auto wrong_payload = build_device_auth_payload(params);

    CHECK_FALSE(verify_device_signature(
        identity.public_key_raw_b64url, wrong_payload, signature));
}

TEST_CASE("Signature verification fails with different keypair", "[parity][device]") {
    auto identity1 = generate_device_keypair();
    auto identity2 = generate_device_keypair();

    DeviceAuthParams params{
        .device_id = identity1.device_id,
        .client_id = "c",
        .client_mode = "bridge",
        .role = "operator",
        .scopes = {},
        .signed_at_ms = openclaw::utils::timestamp_ms(),
        .token = "t",
        .nonce = "n",
    };

    auto payload = build_device_auth_payload(params);
    auto signature = sign_device_payload(identity1.private_key_pem, payload);

    // Verify with wrong public key
    CHECK_FALSE(verify_device_signature(
        identity2.public_key_raw_b64url, payload, signature));
}

// ==========================================================================
// base64url encoding parity
// Ref: base64url is standard base64 with + → -, / → _, no = padding
// ==========================================================================

TEST_CASE("base64url: no padding, no + or /", "[parity][device]") {
    // Use data that produces +, /, and = in standard base64
    std::string data(256, '\0');
    for (int i = 0; i < 256; ++i) data[i] = static_cast<char>(i);

    auto encoded = base64url_encode(data);

    CHECK(encoded.find('+') == std::string::npos);
    CHECK(encoded.find('/') == std::string::npos);
    CHECK(encoded.find('=') == std::string::npos);
}

TEST_CASE("base64url: encode/decode round-trip", "[parity][device]") {
    std::string original = "hello world, this is a test of base64url encoding!";
    auto encoded = base64url_encode(original);
    auto decoded = base64url_decode(encoded);
    CHECK(decoded == original);
}

// ==========================================================================
// Frame round-trip serialization parity
// Verifies that serialize → parse produces identical frames
// ==========================================================================

TEST_CASE("Request frame serialize → parse round-trip", "[parity][frame]") {
    RequestFrame original{
        .id = "parity-req-1",
        .method = "connect",
        .params = json{
            {"minProtocol", 3},
            {"maxProtocol", 3},
            {"client", "mylobster-bridge/1.0"},
        },
    };

    auto serialized = serialize_frame(Frame{original});
    auto parsed = parse_frame(serialized);

    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<RequestFrame>(*parsed));
    auto& restored = std::get<RequestFrame>(*parsed);

    CHECK(restored.id == "parity-req-1");
    CHECK(restored.method == "connect");
    CHECK(restored.params["minProtocol"] == 3);
}

TEST_CASE("Response frame serialize → parse round-trip preserves payload", "[parity][frame]") {
    auto original = make_response("parity-res-1", json{
        {"type", "hello-ok"},
        {"protocol", 3},
    });

    auto serialized = serialize_frame(Frame{original});

    // Verify wire format
    auto wire = json::parse(serialized);
    CHECK(wire["type"] == "res");
    CHECK(wire.contains("payload"));
    CHECK_FALSE(wire.contains("result"));

    // Parse back
    auto parsed = parse_frame(serialized);
    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<ResponseFrame>(*parsed));
    auto& restored = std::get<ResponseFrame>(*parsed);

    CHECK(restored.ok == true);
    REQUIRE(restored.result.has_value());
    CHECK((*restored.result)["type"] == "hello-ok");
    CHECK((*restored.result)["protocol"] == 3);
}

TEST_CASE("Error response serialize → parse round-trip", "[parity][frame]") {
    auto original = make_error_response("parity-err-1",
        openclaw::ErrorCode::Unauthorized, "auth failed");

    auto serialized = serialize_frame(Frame{original});
    auto wire = json::parse(serialized);

    // Verify error code is string
    CHECK(wire["error"]["code"].is_string());
    CHECK(wire["error"]["code"] == "UNAUTHORIZED");
    CHECK(wire["ok"] == false);

    // Parse back
    auto parsed = parse_frame(serialized);
    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<ResponseFrame>(*parsed));
    auto& restored = std::get<ResponseFrame>(*parsed);

    CHECK(restored.ok == false);
    CHECK(restored.is_error());
}

// ==========================================================================
// Frame type inference parity (without explicit "type" field)
// Ref: OpenClaw clients may omit the "type" field
// ==========================================================================

TEST_CASE("Infer request from 'method' field", "[parity][frame]") {
    auto raw = R"({"id":"r1","method":"test","params":{}})";
    auto result = parse_frame(raw);
    REQUIRE(result.has_value());
    CHECK(std::holds_alternative<RequestFrame>(*result));
}

TEST_CASE("Infer response from 'payload' field", "[parity][frame]") {
    auto raw = R"({"id":"r1","payload":{"ok":true}})";
    auto result = parse_frame(raw);
    REQUIRE(result.has_value());
    CHECK(std::holds_alternative<ResponseFrame>(*result));
}

TEST_CASE("Infer response from legacy 'result' field", "[parity][frame]") {
    auto raw = R"({"id":"r1","result":{"ok":true}})";
    auto result = parse_frame(raw);
    REQUIRE(result.has_value());
    CHECK(std::holds_alternative<ResponseFrame>(*result));
}

TEST_CASE("Infer event from 'event' field", "[parity][frame]") {
    auto raw = R"({"event":"tick","payload":{"ts":123}})";
    auto result = parse_frame(raw);
    REQUIRE(result.has_value());
    CHECK(std::holds_alternative<EventFrame>(*result));
}

// ==========================================================================
// ConnectParams schema parity (structural match to ConnectParamsSchema)
// Ref: openclaw/src/gateway/protocol/schema/frames.ts:20-69
// ==========================================================================

TEST_CASE("Connect request params structure matches OpenClaw schema", "[parity][handshake]") {
    // Build a connect request that matches ConnectParamsSchema
    auto identity = generate_device_keypair();
    auto nonce = openclaw::utils::generate_uuid();
    auto signed_at = openclaw::utils::timestamp_ms();
    std::string token = "test-jwt";

    DeviceAuthParams auth_params{
        .device_id = identity.device_id,
        .client_id = "mylobster-bridge",
        .client_mode = "bridge",
        .role = "operator",
        .scopes = {"operator.write", "chat.send"},
        .signed_at_ms = signed_at,
        .token = token,
        .nonce = nonce,
    };

    auto payload = build_device_auth_payload(auth_params);
    auto signature = sign_device_payload(identity.private_key_pem, payload);

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
        }},
    };

    RequestFrame req{
        .id = "connect-1",
        .method = "connect",
        .params = connect_params,
    };

    // Verify it serializes and parses correctly
    auto serialized = serialize_frame(Frame{req});
    auto parsed = parse_frame(serialized);
    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<RequestFrame>(*parsed));

    auto& restored = std::get<RequestFrame>(*parsed);
    CHECK(restored.method == "connect");
    CHECK(restored.params["minProtocol"] == 3);
    CHECK(restored.params["device"]["id"] == identity.device_id);
    CHECK(restored.params["device"]["nonce"] == nonce);

    // Verify the device signature can be re-verified from the serialized params
    auto dev = restored.params["device"];
    auto derived_id = derive_device_id_from_public_key(
        dev["publicKey"].get<std::string>());
    CHECK(derived_id == identity.device_id);

    CHECK(verify_device_signature(
        dev["publicKey"].get<std::string>(),
        payload,
        dev["signature"].get<std::string>()));
}
