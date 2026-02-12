#include <catch2/catch_test_macros.hpp>

#include "openclaw/gateway/frame.hpp"

using namespace openclaw::gateway;
using json = nlohmann::json;

TEST_CASE("RequestFrame serialization", "[frame]") {
    SECTION("to_json produces expected fields") {
        RequestFrame req{
            .id = "req-001",
            .method = "chat.send",
            .params = json{{"text", "hello"}},
        };

        json j;
        to_json(j, req);

        CHECK(j["type"] == "request");
        CHECK(j["id"] == "req-001");
        CHECK(j["method"] == "chat.send");
        CHECK(j["params"]["text"] == "hello");
    }

    SECTION("from_json parses correctly") {
        json j = {
            {"id", "req-002"},
            {"method", "tool.execute"},
            {"params", {{"name", "calculator"}}},
        };

        RequestFrame req;
        from_json(j, req);

        CHECK(req.id == "req-002");
        CHECK(req.method == "tool.execute");
        CHECK(req.params["name"] == "calculator");
    }

    SECTION("from_json defaults params to empty object when missing") {
        json j = {
            {"id", "req-003"},
            {"method", "system.ping"},
        };

        RequestFrame req;
        from_json(j, req);

        CHECK(req.params.is_object());
        CHECK(req.params.empty());
    }
}

TEST_CASE("ResponseFrame serialization", "[frame]") {
    SECTION("success response") {
        auto resp = make_response("req-001", json{{"status", "ok"}});

        json j;
        to_json(j, resp);

        CHECK(j["type"] == "response");
        CHECK(j["id"] == "req-001");
        CHECK(j["result"]["status"] == "ok");
        CHECK_FALSE(j.contains("error"));
    }

    SECTION("error response") {
        auto resp = make_error_response("req-002",
                                         openclaw::ErrorCode::NotFound,
                                         "method not found");

        CHECK(resp.is_error());

        json j;
        to_json(j, resp);

        CHECK(j["type"] == "response");
        CHECK(j["id"] == "req-002");
        CHECK_FALSE(j.contains("result"));
        CHECK(j["error"]["message"] == "method not found");
    }

    SECTION("from_json parses success response") {
        json j = {
            {"id", "resp-001"},
            {"result", {{"value", 42}}},
        };

        ResponseFrame resp;
        from_json(j, resp);

        CHECK(resp.id == "resp-001");
        REQUIRE(resp.result.has_value());
        CHECK((*resp.result)["value"] == 42);
        CHECK_FALSE(resp.error.has_value());
        CHECK_FALSE(resp.is_error());
    }

    SECTION("from_json parses error response") {
        json j = {
            {"id", "resp-002"},
            {"error", {{"code", 4}, {"message", "not found"}}},
        };

        ResponseFrame resp;
        from_json(j, resp);

        CHECK(resp.is_error());
        REQUIRE(resp.error.has_value());
        CHECK((*resp.error)["message"] == "not found");
    }
}

TEST_CASE("EventFrame serialization", "[frame]") {
    SECTION("to_json") {
        auto evt = make_event("session.created", json{{"session_id", "s1"}});

        json j;
        to_json(j, evt);

        CHECK(j["type"] == "event");
        CHECK(j["event"] == "session.created");
        CHECK(j["data"]["session_id"] == "s1");
    }

    SECTION("from_json") {
        json j = {
            {"event", "message.received"},
            {"data", {{"text", "hi"}}},
        };

        EventFrame evt;
        from_json(j, evt);

        CHECK(evt.event == "message.received");
        CHECK(evt.data["text"] == "hi");
    }

    SECTION("from_json defaults data to empty object") {
        json j = {{"event", "ping"}};

        EventFrame evt;
        from_json(j, evt);

        CHECK(evt.data.is_object());
        CHECK(evt.data.empty());
    }

    SECTION("make_event with default data") {
        auto evt = make_event("heartbeat");
        CHECK(evt.event == "heartbeat");
        CHECK(evt.data.is_object());
    }
}

TEST_CASE("parse_frame dispatches to correct type", "[frame]") {
    SECTION("request frame with explicit type") {
        auto raw = R"({"type":"request","id":"r1","method":"test.ping","params":{}})";
        auto result = parse_frame(raw);

        REQUIRE(result.has_value());
        REQUIRE(std::holds_alternative<RequestFrame>(*result));
        auto& req = std::get<RequestFrame>(*result);
        CHECK(req.id == "r1");
        CHECK(req.method == "test.ping");
    }

    SECTION("request frame inferred from method field") {
        auto raw = R"({"id":"r2","method":"test.echo","params":{"x":1}})";
        auto result = parse_frame(raw);

        REQUIRE(result.has_value());
        REQUIRE(std::holds_alternative<RequestFrame>(*result));
    }

    SECTION("response frame inferred from result") {
        auto raw = R"({"id":"r3","result":{"ok":true}})";
        auto result = parse_frame(raw);

        REQUIRE(result.has_value());
        REQUIRE(std::holds_alternative<ResponseFrame>(*result));
    }

    SECTION("event frame inferred from event field") {
        auto raw = R"({"event":"status.update","data":{}})";
        auto result = parse_frame(raw);

        REQUIRE(result.has_value());
        REQUIRE(std::holds_alternative<EventFrame>(*result));
    }

    SECTION("malformed JSON returns error") {
        auto result = parse_frame("not json{");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == openclaw::ErrorCode::SerializationError);
    }

    SECTION("indeterminate frame returns protocol error") {
        auto raw = R"({"id":"x","data":"something"})";
        auto result = parse_frame(raw);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == openclaw::ErrorCode::ProtocolError);
    }
}

TEST_CASE("serialize_frame round-trips", "[frame]") {
    SECTION("request round-trip") {
        RequestFrame original{.id = "rt1", .method = "foo", .params = json{{"k", "v"}}};
        Frame frame = original;
        auto serialized = serialize_frame(frame);
        auto parsed = parse_frame(serialized);

        REQUIRE(parsed.has_value());
        REQUIRE(std::holds_alternative<RequestFrame>(*parsed));
        auto& restored = std::get<RequestFrame>(*parsed);
        CHECK(restored.id == "rt1");
        CHECK(restored.method == "foo");
        CHECK(restored.params["k"] == "v");
    }

    SECTION("event round-trip") {
        auto original = make_event("test.evt", json{{"n", 42}});
        Frame frame = original;
        auto serialized = serialize_frame(frame);
        auto parsed = parse_frame(serialized);

        REQUIRE(parsed.has_value());
        REQUIRE(std::holds_alternative<EventFrame>(*parsed));
        auto& restored = std::get<EventFrame>(*parsed);
        CHECK(restored.event == "test.evt");
        CHECK(restored.data["n"] == 42);
    }
}
