#include <catch2/catch_test_macros.hpp>

#include "openclaw/routing/rules.hpp"

using namespace openclaw::routing;

TEST_CASE("ScopeRule peer matching", "[routing][scope]") {
    ScopeRule rule(BindingScope::Peer, "user123");

    SECTION("Matches correct peer") {
        IncomingMessage msg;
        msg.channel = "discord";
        msg.sender_id = "user123";
        msg.text = "hello";
        msg.binding = BindingContext{.peer_id = "user123"};
        CHECK(rule.matches(msg));
    }

    SECTION("Does not match different peer") {
        IncomingMessage msg;
        msg.channel = "discord";
        msg.sender_id = "user456";
        msg.text = "hello";
        msg.binding = BindingContext{.peer_id = "user456"};
        CHECK_FALSE(rule.matches(msg));
    }

    SECTION("Does not match without binding context") {
        IncomingMessage msg;
        msg.channel = "discord";
        msg.sender_id = "user123";
        msg.text = "hello";
        CHECK_FALSE(rule.matches(msg));
    }
}

TEST_CASE("ScopeRule guild matching", "[routing][scope]") {
    ScopeRule rule(BindingScope::Guild, "guild789");

    SECTION("Matches correct guild") {
        IncomingMessage msg;
        msg.text = "hello";
        msg.binding = BindingContext{
            .peer_id = "user123",
            .guild_id = "guild789",
        };
        CHECK(rule.matches(msg));
    }

    SECTION("Does not match different guild") {
        IncomingMessage msg;
        msg.text = "hello";
        msg.binding = BindingContext{
            .peer_id = "user123",
            .guild_id = "guild999",
        };
        CHECK_FALSE(rule.matches(msg));
    }

    SECTION("Does not match without guild_id") {
        IncomingMessage msg;
        msg.text = "hello";
        msg.binding = BindingContext{.peer_id = "user123"};
        CHECK_FALSE(rule.matches(msg));
    }
}

TEST_CASE("ScopeRule team matching", "[routing][scope]") {
    ScopeRule rule(BindingScope::Team, "team456");

    SECTION("Matches correct team") {
        IncomingMessage msg;
        msg.text = "hello";
        msg.binding = BindingContext{
            .peer_id = "user123",
            .team_id = "team456",
        };
        CHECK(rule.matches(msg));
    }
}

TEST_CASE("ScopeRule global matching", "[routing][scope]") {
    ScopeRule rule(BindingScope::Global, "");

    SECTION("Matches anything") {
        IncomingMessage msg;
        msg.text = "hello";
        CHECK(rule.matches(msg));
    }

    SECTION("Matches with binding context") {
        IncomingMessage msg;
        msg.text = "hello";
        msg.binding = BindingContext{.peer_id = "user123"};
        CHECK(rule.matches(msg));
    }
}

TEST_CASE("ScopeRule naming", "[routing][scope]") {
    CHECK(ScopeRule(BindingScope::Peer, "u1").name() == "scope:peer:u1");
    CHECK(ScopeRule(BindingScope::Guild, "g1").name() == "scope:guild:g1");
    CHECK(ScopeRule(BindingScope::Team, "t1").name() == "scope:team:t1");
    CHECK(ScopeRule(BindingScope::Global, "").name() == "scope:global");
}
