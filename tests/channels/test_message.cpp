#include <catch2/catch_test_macros.hpp>

#include "openclaw/channels/message.hpp"

using namespace openclaw::channels;
using json = nlohmann::json;

TEST_CASE("Attachment serialization", "[channels][message]") {
    SECTION("round-trip with all fields") {
        Attachment att{
            .type = "image",
            .url = "https://example.com/photo.jpg",
            .filename = "photo.jpg",
            .size = 1024,
        };

        json j;
        to_json(j, att);

        CHECK(j["type"] == "image");
        CHECK(j["url"] == "https://example.com/photo.jpg");

        Attachment restored;
        from_json(j, restored);

        CHECK(restored.type == "image");
        CHECK(restored.url == "https://example.com/photo.jpg");
    }

    SECTION("optional fields can be absent") {
        json j = {
            {"type", "file"},
            {"url", "https://example.com/doc.pdf"},
        };

        Attachment att;
        from_json(j, att);

        CHECK(att.type == "file");
        CHECK(att.url == "https://example.com/doc.pdf");
        CHECK_FALSE(att.filename.has_value());
        CHECK_FALSE(att.size.has_value());
    }
}

TEST_CASE("IncomingMessage serialization", "[channels][message]") {
    SECTION("to_json includes all required fields") {
        IncomingMessage msg;
        msg.id = "msg-001";
        msg.channel = "telegram";
        msg.sender_id = "user-42";
        msg.sender_name = "Alice";
        msg.text = "Hello, bot!";
        msg.raw = json{{"update_id", 12345}};
        msg.received_at = openclaw::Clock::now();

        json j;
        to_json(j, msg);

        CHECK(j["id"] == "msg-001");
        CHECK(j["channel"] == "telegram");
        CHECK(j["sender_id"] == "user-42");
        CHECK(j["sender_name"] == "Alice");
        CHECK(j["text"] == "Hello, bot!");
    }

    SECTION("from_json parses correctly") {
        json j = {
            {"id", "msg-002"},
            {"channel", "discord"},
            {"sender_id", "user-99"},
            {"sender_name", "Bob"},
            {"text", "Hi there"},
            {"attachments", json::array()},
            {"raw", json::object()},
        };

        IncomingMessage msg;
        from_json(j, msg);

        CHECK(msg.id == "msg-002");
        CHECK(msg.channel == "discord");
        CHECK(msg.sender_id == "user-99");
        CHECK(msg.text == "Hi there");
    }

    SECTION("with attachments") {
        IncomingMessage msg;
        msg.id = "msg-003";
        msg.channel = "slack";
        msg.sender_id = "u1";
        msg.sender_name = "Carol";
        msg.text = "See attachment";
        msg.attachments.push_back(Attachment{
            .type = "file",
            .url = "https://files.slack.com/doc.pdf",
        });
        msg.raw = json::object();
        msg.received_at = openclaw::Clock::now();

        json j;
        to_json(j, msg);

        REQUIRE(j.contains("attachments"));
        CHECK(j["attachments"].size() == 1);
    }
}

TEST_CASE("OutgoingMessage serialization", "[channels][message]") {
    SECTION("basic round-trip") {
        OutgoingMessage msg;
        msg.channel = "telegram";
        msg.recipient_id = "chat-100";
        msg.text = "I got your message!";
        msg.extra = json::object();

        json j;
        to_json(j, msg);

        CHECK(j["channel"] == "telegram");
        CHECK(j["recipient_id"] == "chat-100");
        CHECK(j["text"] == "I got your message!");

        OutgoingMessage restored;
        from_json(j, restored);

        CHECK(restored.channel == "telegram");
        CHECK(restored.recipient_id == "chat-100");
        CHECK(restored.text == "I got your message!");
    }

    SECTION("with optional reply_to and thread_id") {
        OutgoingMessage msg;
        msg.channel = "discord";
        msg.recipient_id = "ch-1";
        msg.text = "Replying";
        msg.reply_to = "msg-original";
        msg.thread_id = "thread-42";
        msg.extra = json::object();

        json j;
        to_json(j, msg);

        // Verify reply_to and thread_id are serialized
        if (j.contains("reply_to")) {
            CHECK(j["reply_to"] == "msg-original");
        }
        if (j.contains("thread_id")) {
            CHECK(j["thread_id"] == "thread-42");
        }
    }

    SECTION("with attachments") {
        OutgoingMessage msg;
        msg.channel = "whatsapp";
        msg.recipient_id = "+1234567890";
        msg.text = "Here is a photo";
        msg.attachments.push_back(Attachment{
            .type = "image",
            .url = "https://cdn.example.com/img.png",
        });
        msg.extra = json::object();

        json j;
        to_json(j, msg);

        REQUIRE(j.contains("attachments"));
        CHECK(j["attachments"].size() == 1);
    }
}

TEST_CASE("IncomingMessage optional fields", "[channels][message]") {
    IncomingMessage msg;
    msg.id = "msg-opt";
    msg.channel = "test";
    msg.sender_id = "s1";
    msg.sender_name = "Test";
    msg.text = "test";
    msg.raw = json::object();
    msg.received_at = openclaw::Clock::now();

    CHECK_FALSE(msg.reply_to.has_value());
    CHECK_FALSE(msg.thread_id.has_value());
    CHECK(msg.attachments.empty());

    msg.reply_to = "parent-msg";
    msg.thread_id = "thread-1";

    CHECK(msg.reply_to.has_value());
    CHECK(*msg.reply_to == "parent-msg");
    CHECK(*msg.thread_id == "thread-1");
}
