#include <catch2/catch_test_macros.hpp>

#include "openclaw/gateway/protocol.hpp"

TEST_CASE("Protocol registers and looks up methods", "[protocol]") {
    openclaw::gateway::Protocol proto;

    bool handler_called = false;
    proto.register_method("test.echo",
        [&handler_called](openclaw::gateway::json params) -> boost::asio::awaitable<openclaw::gateway::json> {
            handler_called = true;
            co_return params;
        },
        "Echo back params",
        "test");

    SECTION("has_method returns true for registered method") {
        CHECK(proto.has_method("test.echo"));
    }

    SECTION("has_method returns false for unknown method") {
        CHECK_FALSE(proto.has_method("nonexistent.method"));
    }
}

TEST_CASE("Protocol lists registered methods", "[protocol]") {
    openclaw::gateway::Protocol proto;

    proto.register_method("alpha.one",
        [](openclaw::gateway::json) -> boost::asio::awaitable<openclaw::gateway::json> {
            co_return openclaw::gateway::json{};
        },
        "First method", "alpha");

    proto.register_method("beta.two",
        [](openclaw::gateway::json) -> boost::asio::awaitable<openclaw::gateway::json> {
            co_return openclaw::gateway::json{};
        },
        "Second method", "beta");

    proto.register_method("alpha.three",
        [](openclaw::gateway::json) -> boost::asio::awaitable<openclaw::gateway::json> {
            co_return openclaw::gateway::json{};
        },
        "Third method", "alpha");

    SECTION("methods() returns all registered methods") {
        auto all = proto.methods();
        REQUIRE(all.size() == 3);
    }

    SECTION("methods_in_group filters by group") {
        auto alpha_methods = proto.methods_in_group("alpha");
        REQUIRE(alpha_methods.size() == 2);
        for (const auto& m : alpha_methods) {
            CHECK(m.group == "alpha");
        }

        auto beta_methods = proto.methods_in_group("beta");
        REQUIRE(beta_methods.size() == 1);
        CHECK(beta_methods[0].name == "beta.two");
    }

    SECTION("methods_in_group returns empty for unknown group") {
        auto empty = proto.methods_in_group("nonexistent");
        CHECK(empty.empty());
    }
}

TEST_CASE("Protocol register_method replaces existing", "[protocol]") {
    openclaw::gateway::Protocol proto;

    proto.register_method("test.method",
        [](openclaw::gateway::json) -> boost::asio::awaitable<openclaw::gateway::json> {
            co_return openclaw::gateway::json{{"version", 1}};
        },
        "Version 1");

    // Replace with new handler
    proto.register_method("test.method",
        [](openclaw::gateway::json) -> boost::asio::awaitable<openclaw::gateway::json> {
            co_return openclaw::gateway::json{{"version", 2}};
        },
        "Version 2");

    auto all = proto.methods();
    REQUIRE(all.size() == 1);
    CHECK(all[0].description == "Version 2");
}

TEST_CASE("Protocol register_builtins populates methods", "[protocol]") {
    openclaw::gateway::Protocol proto;
    proto.register_builtins();

    auto all = proto.methods();
    // Builtins should register at least a few methods
    CHECK(all.size() > 0);
}

TEST_CASE("Protocol method info preserves metadata", "[protocol]") {
    openclaw::gateway::Protocol proto;

    proto.register_method("chat.send",
        [](openclaw::gateway::json) -> boost::asio::awaitable<openclaw::gateway::json> {
            co_return openclaw::gateway::json{};
        },
        "Send a chat message",
        "chat");

    auto all = proto.methods();
    REQUIRE(all.size() == 1);
    CHECK(all[0].name == "chat.send");
    CHECK(all[0].description == "Send a chat message");
    CHECK(all[0].group == "chat");
}
