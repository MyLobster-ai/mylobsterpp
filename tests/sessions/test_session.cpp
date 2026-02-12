#include <catch2/catch_test_macros.hpp>

#include "openclaw/sessions/session.hpp"

using namespace openclaw::sessions;
using json = nlohmann::json;

TEST_CASE("SessionState JSON serialization", "[sessions]") {
    SECTION("Active") {
        json j = SessionState::Active;
        CHECK(j == "active");
    }

    SECTION("Idle") {
        json j = SessionState::Idle;
        CHECK(j == "idle");
    }

    SECTION("Closed") {
        json j = SessionState::Closed;
        CHECK(j == "closed");
    }

    SECTION("round-trip Active") {
        json j = "active";
        auto state = j.get<SessionState>();
        CHECK(state == SessionState::Active);
    }

    SECTION("round-trip Idle") {
        json j = "idle";
        auto state = j.get<SessionState>();
        CHECK(state == SessionState::Idle);
    }

    SECTION("round-trip Closed") {
        json j = "closed";
        auto state = j.get<SessionState>();
        CHECK(state == SessionState::Closed);
    }
}

TEST_CASE("SessionData creation with defaults", "[sessions]") {
    SessionData data;

    CHECK(data.state == SessionState::Active);
    CHECK(data.metadata.is_null());
    CHECK(data.session.id.empty());
    CHECK(data.session.user_id.empty());
    CHECK(data.session.device_id.empty());
    CHECK_FALSE(data.session.channel.has_value());
}

TEST_CASE("SessionData state transitions", "[sessions]") {
    SessionData data;
    data.session.id = "sess-001";
    data.session.user_id = "user-42";
    data.session.device_id = "dev-abc";
    data.state = SessionState::Active;

    SECTION("Active to Idle") {
        data.state = SessionState::Idle;
        CHECK(data.state == SessionState::Idle);
    }

    SECTION("Active to Closed") {
        data.state = SessionState::Closed;
        CHECK(data.state == SessionState::Closed);
    }

    SECTION("Idle to Active") {
        data.state = SessionState::Idle;
        data.state = SessionState::Active;
        CHECK(data.state == SessionState::Active);
    }

    SECTION("Idle to Closed") {
        data.state = SessionState::Idle;
        data.state = SessionState::Closed;
        CHECK(data.state == SessionState::Closed);
    }
}

TEST_CASE("SessionData to_json serialization", "[sessions]") {
    SessionData data;
    data.session.id = "sess-100";
    data.session.user_id = "user-200";
    data.session.device_id = "dev-300";
    data.session.channel = "telegram";
    data.state = SessionState::Active;
    data.metadata = json{{"key", "value"}};
    data.session.created_at = openclaw::Clock::now();
    data.session.last_active = openclaw::Clock::now();

    json j;
    to_json(j, data);

    CHECK(j["id"] == "sess-100");
    CHECK(j["user_id"] == "user-200");
    CHECK(j["device_id"] == "dev-300");
    CHECK(j["channel"] == "telegram");
    CHECK(j["state"] == "active");
    CHECK(j["metadata"]["key"] == "value");
}

TEST_CASE("SessionData from_json deserialization", "[sessions]") {
    json j = {
        {"id", "sess-500"},
        {"user_id", "user-600"},
        {"device_id", "dev-700"},
        {"state", "idle"},
        {"metadata", {{"count", 5}}},
        {"channel", "discord"},
    };

    SessionData data;
    from_json(j, data);

    CHECK(data.session.id == "sess-500");
    CHECK(data.session.user_id == "user-600");
    CHECK(data.session.device_id == "dev-700");
    CHECK(data.state == SessionState::Idle);
    CHECK(data.metadata["count"] == 5);
    REQUIRE(data.session.channel.has_value());
    CHECK(*data.session.channel == "discord");
}

TEST_CASE("SessionData without optional channel", "[sessions]") {
    json j = {
        {"id", "sess-800"},
        {"user_id", "user-900"},
        {"device_id", "dev-1000"},
        {"state", "closed"},
        {"metadata", json::object()},
    };

    SessionData data;
    from_json(j, data);

    CHECK_FALSE(data.session.channel.has_value());
    CHECK(data.state == SessionState::Closed);
}
