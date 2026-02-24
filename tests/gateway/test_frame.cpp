#include <catch2/catch_test_macros.hpp>

#include "openclaw/gateway/frame.hpp"

using namespace openclaw::gateway;
using json = nlohmann::json;

TEST_CASE("RequestFrame serialization", "[frame]") {
    SECTION("to_json produces expected fields with 'req' type") {
        RequestFrame req{
            .id = "req-001",
            .method = "chat.send",
            .params = json{{"text", "hello"}},
        };

        json j;
        to_json(j, req);

        CHECK(j["type"] == "req");
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
    SECTION("success response has 'res' type and ok=true") {
        auto resp = make_response("req-001", json{{"status", "ok"}});

        json j;
        to_json(j, resp);

        CHECK(j["type"] == "res");
        CHECK(j["id"] == "req-001");
        CHECK(j["ok"] == true);
        CHECK(j["result"]["status"] == "ok");
        CHECK_FALSE(j.contains("error"));
    }

    SECTION("error response has ok=false") {
        auto resp = make_error_response("req-002",
                                         openclaw::ErrorCode::NotFound,
                                         "method not found");

        CHECK(resp.is_error());
        CHECK(resp.ok == false);

        json j;
        to_json(j, resp);

        CHECK(j["type"] == "res");
        CHECK(j["id"] == "req-002");
        CHECK(j["ok"] == false);
        CHECK_FALSE(j.contains("result"));
        CHECK(j["error"]["message"] == "method not found");
    }

    SECTION("from_json parses success response with ok field") {
        json j = {
            {"id", "resp-001"},
            {"ok", true},
            {"result", {{"value", 42}}},
        };

        ResponseFrame resp;
        from_json(j, resp);

        CHECK(resp.id == "resp-001");
        CHECK(resp.ok == true);
        REQUIRE(resp.result.has_value());
        CHECK((*resp.result)["value"] == 42);
        CHECK_FALSE(resp.error.has_value());
        CHECK_FALSE(resp.is_error());
    }

    SECTION("from_json defaults ok to true when missing") {
        json j = {
            {"id", "resp-003"},
            {"result", {{"value", 1}}},
        };

        ResponseFrame resp;
        from_json(j, resp);

        CHECK(resp.ok == true);
    }

    SECTION("from_json parses error response") {
        json j = {
            {"id", "resp-002"},
            {"ok", false},
            {"error", {{"code", 4}, {"message", "not found"}}},
        };

        ResponseFrame resp;
        from_json(j, resp);

        CHECK(resp.is_error());
        CHECK(resp.ok == false);
        REQUIRE(resp.error.has_value());
        CHECK((*resp.error)["message"] == "not found");
    }
}

TEST_CASE("EventFrame serialization", "[frame]") {
    SECTION("to_json uses 'payload' field") {
        auto evt = make_event("session.created", json{{"session_id", "s1"}});

        json j;
        to_json(j, evt);

        CHECK(j["type"] == "event");
        CHECK(j["event"] == "session.created");
        CHECK(j["payload"]["session_id"] == "s1");
        CHECK_FALSE(j.contains("data"));
    }

    SECTION("from_json parses 'payload' field") {
        json j = {
            {"event", "message.received"},
            {"payload", {{"text", "hi"}}},
        };

        EventFrame evt;
        from_json(j, evt);

        CHECK(evt.event == "message.received");
        CHECK(evt.data["text"] == "hi");
    }

    SECTION("from_json accepts legacy 'data' field for backwards compat") {
        json j = {
            {"event", "legacy.event"},
            {"data", {{"text", "old"}}},
        };

        EventFrame evt;
        from_json(j, evt);

        CHECK(evt.event == "legacy.event");
        CHECK(evt.data["text"] == "old");
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
    SECTION("request frame with 'req' type") {
        auto raw = R"({"type":"req","id":"r1","method":"test.ping","params":{}})";
        auto result = parse_frame(raw);

        REQUIRE(result.has_value());
        REQUIRE(std::holds_alternative<RequestFrame>(*result));
        auto& req = std::get<RequestFrame>(*result);
        CHECK(req.id == "r1");
        CHECK(req.method == "test.ping");
    }

    SECTION("request frame with legacy 'request' type still works") {
        auto raw = R"({"type":"request","id":"r1b","method":"test.ping","params":{}})";
        auto result = parse_frame(raw);

        REQUIRE(result.has_value());
        REQUIRE(std::holds_alternative<RequestFrame>(*result));
        auto& req = std::get<RequestFrame>(*result);
        CHECK(req.id == "r1b");
    }

    SECTION("response frame with 'res' type") {
        auto raw = R"({"type":"res","id":"r3","ok":true,"result":{"ok":true}})";
        auto result = parse_frame(raw);

        REQUIRE(result.has_value());
        REQUIRE(std::holds_alternative<ResponseFrame>(*result));
        auto& resp = std::get<ResponseFrame>(*result);
        CHECK(resp.ok == true);
    }

    SECTION("response frame with legacy 'response' type still works") {
        auto raw = R"({"type":"response","id":"r3b","result":{"ok":true}})";
        auto result = parse_frame(raw);

        REQUIRE(result.has_value());
        REQUIRE(std::holds_alternative<ResponseFrame>(*result));
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
        auto raw = R"({"event":"status.update","payload":{}})";
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

    SECTION("response round-trip preserves ok field") {
        auto original = make_response("rt2", json{{"status", "done"}});
        Frame frame = original;
        auto serialized = serialize_frame(frame);
        auto parsed = parse_frame(serialized);

        REQUIRE(parsed.has_value());
        REQUIRE(std::holds_alternative<ResponseFrame>(*parsed));
        auto& restored = std::get<ResponseFrame>(*parsed);
        CHECK(restored.id == "rt2");
        CHECK(restored.ok == true);
        REQUIRE(restored.result.has_value());
    }

    SECTION("error response round-trip preserves ok=false") {
        auto original = make_error_response("rt3",
            openclaw::ErrorCode::NotFound, "missing");
        Frame frame = original;
        auto serialized = serialize_frame(frame);
        auto parsed = parse_frame(serialized);

        REQUIRE(parsed.has_value());
        REQUIRE(std::holds_alternative<ResponseFrame>(*parsed));
        auto& restored = std::get<ResponseFrame>(*parsed);
        CHECK(restored.ok == false);
        CHECK(restored.is_error());
    }

    SECTION("event round-trip uses payload field") {
        auto original = make_event("test.evt", json{{"n", 42}});
        Frame frame = original;
        auto serialized = serialize_frame(frame);

        // Verify the serialized JSON uses "payload" not "data"
        auto j = json::parse(serialized);
        CHECK(j.contains("payload"));
        CHECK_FALSE(j.contains("data"));

        auto parsed = parse_frame(serialized);
        REQUIRE(parsed.has_value());
        REQUIRE(std::holds_alternative<EventFrame>(*parsed));
        auto& restored = std::get<EventFrame>(*parsed);
        CHECK(restored.event == "test.evt");
        CHECK(restored.data["n"] == 42);
    }
}
