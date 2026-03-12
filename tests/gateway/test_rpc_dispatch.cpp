#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>

#include "openclaw/gateway/protocol.hpp"

using namespace openclaw::gateway;
using json = nlohmann::json;

/// Helper to dispatch an RPC method synchronously for testing.
static auto dispatch_sync(Protocol& protocol, const std::string& method,
                           const json& params = json::object()) -> json {
    boost::asio::io_context ioc;
    json result;

    RequestFrame req;
    req.id = "test-1";
    req.method = method;
    req.params = params;

    boost::asio::co_spawn(ioc, [&]() -> boost::asio::awaitable<void> {
        auto res = co_await protocol.dispatch(req);
        if (res) {
            result = *res;
        } else {
            result = json{{"error", "dispatch_failed"}};
        }
    }, boost::asio::detached);

    ioc.run();
    return result;
}

// ---------------------------------------------------------------------------
// sessions.get dispatch
// ---------------------------------------------------------------------------

TEST_CASE("sessions.get returns session state", "[gateway][rpc]") {
    Protocol protocol;
    protocol.register_builtins();

    auto result = dispatch_sync(protocol, "sessions.get",
                                 {{"session_key", "agent:telegram:user123"}});
    CHECK(result["session_key"] == "agent:telegram:user123");
    CHECK(result["status"] == "active");
}

TEST_CASE("sessions.get requires session_key", "[gateway][rpc]") {
    Protocol protocol;
    protocol.register_builtins();

    auto result = dispatch_sync(protocol, "sessions.get", json::object());
    CHECK(result.contains("error"));
}

// ---------------------------------------------------------------------------
// config.schema.lookup dispatch
// ---------------------------------------------------------------------------

TEST_CASE("config.schema.lookup returns schema info", "[gateway][rpc]") {
    Protocol protocol;
    protocol.register_builtins();

    auto result = dispatch_sync(protocol, "config.schema.lookup",
                                 {{"path", "gateway.port"}});
    CHECK(result["path"] == "gateway.port");
    CHECK(result["exists"] == true);
}

TEST_CASE("config.schema.lookup requires path", "[gateway][rpc]") {
    Protocol protocol;
    protocol.register_builtins();

    auto result = dispatch_sync(protocol, "config.schema.lookup", json::object());
    CHECK(result.contains("error"));
}

// ---------------------------------------------------------------------------
// node.pending.enqueue dispatch
// ---------------------------------------------------------------------------

TEST_CASE("node.pending.enqueue returns queued confirmation", "[gateway][rpc]") {
    Protocol protocol;
    protocol.register_builtins();

    auto result = dispatch_sync(protocol, "node.pending.enqueue",
                                 {{"node_id", "node-abc"}, {"work", {{"task", "compile"}}}});
    CHECK(result["queued"] == true);
    CHECK(result["node_id"] == "node-abc");
    CHECK(result["queue_size"] == 1);
}

// ---------------------------------------------------------------------------
// node.pending.drain dispatch
// ---------------------------------------------------------------------------

TEST_CASE("node.pending.drain returns drained items", "[gateway][rpc]") {
    Protocol protocol;
    protocol.register_builtins();

    auto result = dispatch_sync(protocol, "node.pending.drain",
                                 {{"node_id", "node-abc"}});
    CHECK(result["drained"] == 0);
    CHECK(result["node_id"] == "node-abc");
    CHECK(result["items"].is_array());
}

// ---------------------------------------------------------------------------
// sessions.spawn with resumeSessionId
// ---------------------------------------------------------------------------

TEST_CASE("sessions.spawn accepts resumeSessionId parameter", "[gateway][rpc]") {
    Protocol protocol;
    protocol.register_builtins();

    auto result = dispatch_sync(protocol, "sessions.spawn",
                                 {{"agent_id", "agent-1"},
                                  {"resumeSessionId", "existing-session-xyz"},
                                  {"runtime", "acp"}});
    // Should not error; resumeSessionId is accepted
    CHECK_FALSE(result.contains("error"));
    if (result.contains("resumed")) {
        CHECK(result["resumed"] == true);
    }
}

// ---------------------------------------------------------------------------
// Method dispatch error for unknown method
// ---------------------------------------------------------------------------

TEST_CASE("dispatch returns error for unknown method", "[gateway][rpc]") {
    Protocol protocol;
    protocol.register_builtins();

    auto result = dispatch_sync(protocol, "nonexistent.method");
    // Should get an error result (dispatch failure)
    CHECK(result.contains("error"));
}

// ---------------------------------------------------------------------------
// Method count verification
// ---------------------------------------------------------------------------

TEST_CASE("Protocol builtins register 160+ methods", "[gateway][rpc]") {
    Protocol protocol;
    protocol.register_builtins();

    auto all_methods = protocol.methods();
    // v2026.3.11: ~160+ methods total
    CHECK(all_methods.size() >= 160);
}

TEST_CASE("Protocol methods grouped correctly", "[gateway][rpc]") {
    Protocol protocol;
    protocol.register_builtins();

    // Spot-check groups
    auto session_methods = protocol.methods_in_group("session");
    CHECK(session_methods.size() >= 10);

    auto config_methods = protocol.methods_in_group("config");
    CHECK(config_methods.size() >= 7);

    auto node_methods = protocol.methods_in_group("node");
    CHECK(node_methods.size() >= 12);
}
