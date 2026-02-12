#include <catch2/catch_test_macros.hpp>

#include "openclaw/providers/provider.hpp"

using namespace openclaw::providers;
using json = nlohmann::json;

TEST_CASE("CompletionRequest default values", "[providers]") {
    CompletionRequest req;

    CHECK(req.model.empty());
    CHECK(req.messages.empty());
    CHECK_FALSE(req.system_prompt.has_value());
    CHECK_FALSE(req.temperature.has_value());
    CHECK_FALSE(req.max_tokens.has_value());
    CHECK(req.tools.empty());
    CHECK(req.thinking == openclaw::ThinkingMode::None);
}

TEST_CASE("CompletionRequest can be populated", "[providers]") {
    CompletionRequest req;
    req.model = "claude-3-opus";
    req.system_prompt = "You are a helpful assistant.";
    req.temperature = 0.7;
    req.max_tokens = 4096;
    req.thinking = openclaw::ThinkingMode::Extended;

    openclaw::Message msg;
    msg.id = "m1";
    msg.role = openclaw::Role::User;
    msg.content.push_back(openclaw::ContentBlock{
        .type = "text",
        .text = "Hello!",
    });
    msg.created_at = openclaw::Clock::now();
    req.messages.push_back(std::move(msg));

    CHECK(req.model == "claude-3-opus");
    CHECK(req.system_prompt.value() == "You are a helpful assistant.");
    CHECK(req.temperature.value() == 0.7);
    CHECK(req.max_tokens.value() == 4096);
    CHECK(req.messages.size() == 1);
    CHECK(req.thinking == openclaw::ThinkingMode::Extended);
}

TEST_CASE("CompletionResponse can be constructed", "[providers]") {
    CompletionResponse resp;
    resp.model = "claude-3-sonnet";
    resp.input_tokens = 100;
    resp.output_tokens = 250;
    resp.stop_reason = "end_turn";

    resp.message.id = "resp-m1";
    resp.message.role = openclaw::Role::Assistant;
    resp.message.content.push_back(openclaw::ContentBlock{
        .type = "text",
        .text = "Hello! How can I help you?",
    });
    resp.message.created_at = openclaw::Clock::now();

    CHECK(resp.model == "claude-3-sonnet");
    CHECK(resp.input_tokens == 100);
    CHECK(resp.output_tokens == 250);
    CHECK(resp.stop_reason == "end_turn");
    CHECK(resp.message.role == openclaw::Role::Assistant);
    CHECK(resp.message.content.size() == 1);
    CHECK(resp.message.content[0].text == "Hello! How can I help you?");
}

TEST_CASE("CompletionChunk types", "[providers]") {
    SECTION("text chunk") {
        CompletionChunk chunk{
            .type = "text",
            .text = "Hello",
        };
        CHECK(chunk.type == "text");
        CHECK(chunk.text == "Hello");
        CHECK_FALSE(chunk.tool_name.has_value());
    }

    SECTION("tool_use chunk") {
        CompletionChunk chunk{
            .type = "tool_use",
            .text = "",
            .tool_name = "calculator",
            .tool_input = json{{"expression", "2+2"}},
        };
        CHECK(chunk.type == "tool_use");
        CHECK(chunk.tool_name.value() == "calculator");
        REQUIRE(chunk.tool_input.has_value());
        CHECK((*chunk.tool_input)["expression"] == "2+2");
    }

    SECTION("stop chunk") {
        CompletionChunk chunk{
            .type = "stop",
            .text = "",
        };
        CHECK(chunk.type == "stop");
    }
}

TEST_CASE("ThinkingMode JSON serialization", "[providers]") {
    SECTION("None") {
        json j = openclaw::ThinkingMode::None;
        CHECK(j == "none");
    }

    SECTION("Basic") {
        json j = openclaw::ThinkingMode::Basic;
        CHECK(j == "basic");
    }

    SECTION("Extended") {
        json j = openclaw::ThinkingMode::Extended;
        CHECK(j == "extended");
    }

    SECTION("round-trip") {
        json j = "extended";
        auto mode = j.get<openclaw::ThinkingMode>();
        CHECK(mode == openclaw::ThinkingMode::Extended);
    }
}

TEST_CASE("Role JSON serialization", "[providers]") {
    SECTION("user role") {
        json j = openclaw::Role::User;
        CHECK(j == "user");
    }

    SECTION("assistant role") {
        json j = openclaw::Role::Assistant;
        CHECK(j == "assistant");
    }

    SECTION("system role") {
        json j = openclaw::Role::System;
        CHECK(j == "system");
    }

    SECTION("tool role") {
        json j = openclaw::Role::Tool;
        CHECK(j == "tool");
    }
}
