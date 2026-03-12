#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "openclaw/channels/matrix.hpp"

using namespace openclaw::channels;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// MatrixChannel::resolve_room_agent (v2026.3.7)
// ---------------------------------------------------------------------------

TEST_CASE("MatrixChannel resolve_room_agent with no bindings", "[channels][matrix]") {
    boost::asio::io_context ioc;
    MatrixConfig config;
    config.homeserver_url = "https://matrix.org";
    config.access_token = "test_token";
    // room_bindings defaults to null JSON

    MatrixChannel channel(config, ioc);
    auto result = channel.resolve_room_agent("!room:matrix.org");
    CHECK_FALSE(result.has_value());
}

TEST_CASE("MatrixChannel resolve_room_agent with string binding", "[channels][matrix]") {
    boost::asio::io_context ioc;
    MatrixConfig config;
    config.homeserver_url = "https://matrix.org";
    config.access_token = "test_token";
    config.room_bindings = json{
        {"!room1:matrix.org", "agent-alpha"},
        {"!room2:matrix.org", "agent-beta"},
    };

    MatrixChannel channel(config, ioc);

    SECTION("existing room returns agent ID") {
        auto result = channel.resolve_room_agent("!room1:matrix.org");
        REQUIRE(result.has_value());
        CHECK(*result == "agent-alpha");
    }

    SECTION("another room") {
        auto result = channel.resolve_room_agent("!room2:matrix.org");
        REQUIRE(result.has_value());
        CHECK(*result == "agent-beta");
    }

    SECTION("unknown room returns nullopt") {
        auto result = channel.resolve_room_agent("!unknown:matrix.org");
        CHECK_FALSE(result.has_value());
    }
}

TEST_CASE("MatrixChannel resolve_room_agent with object binding", "[channels][matrix]") {
    boost::asio::io_context ioc;
    MatrixConfig config;
    config.homeserver_url = "https://matrix.org";
    config.access_token = "test_token";
    config.room_bindings = json{
        {"!room1:matrix.org", {{"agentId", "agent-gamma"}, {"mode", "full"}}},
    };

    MatrixChannel channel(config, ioc);
    auto result = channel.resolve_room_agent("!room1:matrix.org");
    REQUIRE(result.has_value());
    CHECK(*result == "agent-gamma");
}

TEST_CASE("MatrixChannel resolve_room_agent object without agentId", "[channels][matrix]") {
    boost::asio::io_context ioc;
    MatrixConfig config;
    config.homeserver_url = "https://matrix.org";
    config.access_token = "test_token";
    config.room_bindings = json{
        {"!room1:matrix.org", {{"mode", "read-only"}}},
    };

    MatrixChannel channel(config, ioc);
    auto result = channel.resolve_room_agent("!room1:matrix.org");
    CHECK_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// MatrixChannel::is_dm_room (v2026.3.7)
// ---------------------------------------------------------------------------

TEST_CASE("MatrixChannel is_dm_room with bound room returns false", "[channels][matrix]") {
    boost::asio::io_context ioc;
    MatrixConfig config;
    config.homeserver_url = "https://matrix.org";
    config.access_token = "test_token";
    config.room_bindings = json{
        {"!room1:matrix.org", "agent-alpha"},
    };

    MatrixChannel channel(config, ioc);
    // Explicitly bound rooms are NOT classified as DMs
    CHECK_FALSE(channel.is_dm_room("!room1:matrix.org"));
}

TEST_CASE("MatrixChannel is_dm_room without bindings returns false", "[channels][matrix]") {
    boost::asio::io_context ioc;
    MatrixConfig config;
    config.homeserver_url = "https://matrix.org";
    config.access_token = "test_token";

    MatrixChannel channel(config, ioc);
    // Without homeserver query, defaults to false (safe fallback)
    CHECK_FALSE(channel.is_dm_room("!unknown:matrix.org"));
}

// ---------------------------------------------------------------------------
// make_matrix_channel factory
// ---------------------------------------------------------------------------

TEST_CASE("make_matrix_channel creates channel from valid settings", "[channels][matrix]") {
    boost::asio::io_context ioc;
    json settings = {
        {"homeserver_url", "https://matrix.org"},
        {"access_token", "syt_test_token"},
        {"channel_name", "my-matrix"},
        {"user_id", "@bot:matrix.org"},
        {"room_bindings", {{"!room:matrix.org", "agent-1"}}},
    };

    auto channel = make_matrix_channel(settings, ioc);
    REQUIRE(channel != nullptr);
    CHECK(channel->name() == "my-matrix");
    CHECK(channel->type() == "matrix");
}

TEST_CASE("make_matrix_channel returns nullptr for missing fields", "[channels][matrix]") {
    boost::asio::io_context ioc;

    SECTION("missing homeserver_url") {
        json settings = {{"access_token", "token"}};
        auto channel = make_matrix_channel(settings, ioc);
        CHECK(channel == nullptr);
    }
    SECTION("missing access_token") {
        json settings = {{"homeserver_url", "https://matrix.org"}};
        auto channel = make_matrix_channel(settings, ioc);
        CHECK(channel == nullptr);
    }
    SECTION("both missing") {
        json settings = json::object();
        auto channel = make_matrix_channel(settings, ioc);
        CHECK(channel == nullptr);
    }
}
