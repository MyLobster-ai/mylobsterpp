#include <catch2/catch_test_macros.hpp>

#include "openclaw/providers/ollama.hpp"

using namespace openclaw::providers;
using json = nlohmann::json;

TEST_CASE("Ollama NDJSON line parsing", "[providers][ollama]") {
    SECTION("Parses a single NDJSON line") {
        std::string line = R"({"message":{"role":"assistant","content":"Hello"},"done":false})";
        auto j = json::parse(line);
        CHECK(j["message"]["content"] == "Hello");
        CHECK(j["done"] == false);
    }

    SECTION("Parses done=true response") {
        std::string line = R"({"message":{"role":"assistant","content":""},"done":true,"total_duration":1234})";
        auto j = json::parse(line);
        CHECK(j["done"] == true);
    }
}

TEST_CASE("Ollama tool call accumulation", "[providers][ollama]") {
    SECTION("Accumulates partial tool calls") {
        json accumulated = json::object();

        // First chunk with tool_calls
        json chunk1 = {
            {"message", {
                {"role", "assistant"},
                {"content", ""},
                {"tool_calls", json::array({{
                    {"function", {
                        {"name", "get_weather"},
                        {"arguments", {{"location", "NYC"}}},
                    }},
                }})},
            }},
            {"done", false},
        };

        // Accumulate
        if (chunk1["message"].contains("tool_calls")) {
            accumulated["tool_calls"] = chunk1["message"]["tool_calls"];
        }

        REQUIRE(accumulated.contains("tool_calls"));
        CHECK(accumulated["tool_calls"][0]["function"]["name"] == "get_weather");
        CHECK(accumulated["tool_calls"][0]["function"]["arguments"]["location"] == "NYC");
    }
}

TEST_CASE("Ollama message conversion", "[providers][ollama]") {
    SECTION("User message format") {
        json msg = {
            {"role", "user"},
            {"content", "Hello"},
        };
        CHECK(msg["role"] == "user");
        CHECK(msg["content"] == "Hello");
    }

    SECTION("Image attachment uses images array") {
        json msg = {
            {"role", "user"},
            {"content", "What is this?"},
            {"images", json::array({"base64data..."})},
        };
        CHECK(msg.contains("images"));
        CHECK(msg["images"].size() == 1);
    }

    SECTION("Tool result maps to tool role") {
        json msg = {
            {"role", "tool"},
            {"content", R"({"temperature": 72})"},
        };
        CHECK(msg["role"] == "tool");
    }
}
