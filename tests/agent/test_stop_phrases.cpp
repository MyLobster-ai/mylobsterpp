#include <catch2/catch_test_macros.hpp>

#include "openclaw/agent/runtime.hpp"

using openclaw::agent::AgentRuntime;

TEST_CASE("English stop phrases", "[agent][stop_phrases]") {
    CHECK(AgentRuntime::is_stop_phrase("stop openclaw"));
    CHECK(AgentRuntime::is_stop_phrase("please stop"));
    CHECK(AgentRuntime::is_stop_phrase("do not do that"));
    CHECK(AgentRuntime::is_stop_phrase("stop"));
    CHECK(AgentRuntime::is_stop_phrase("cancel"));
    CHECK(AgentRuntime::is_stop_phrase("abort"));
    CHECK(AgentRuntime::is_stop_phrase("quit"));
    CHECK(AgentRuntime::is_stop_phrase("stop it"));
    CHECK(AgentRuntime::is_stop_phrase("stop now"));
}

TEST_CASE("Stop phrases with trailing punctuation tolerance", "[agent][stop_phrases]") {
    CHECK(AgentRuntime::is_stop_phrase("stop!"));
    CHECK(AgentRuntime::is_stop_phrase("stop."));
    CHECK(AgentRuntime::is_stop_phrase("please stop!"));
    CHECK(AgentRuntime::is_stop_phrase("cancel."));
    CHECK(AgentRuntime::is_stop_phrase("abort!"));
    CHECK(AgentRuntime::is_stop_phrase("stop openclaw."));
}

TEST_CASE("Case insensitive English stop phrases", "[agent][stop_phrases]") {
    CHECK(AgentRuntime::is_stop_phrase("STOP"));
    CHECK(AgentRuntime::is_stop_phrase("Stop OpenClaw"));
    CHECK(AgentRuntime::is_stop_phrase("PLEASE STOP"));
    CHECK(AgentRuntime::is_stop_phrase("Cancel"));
    CHECK(AgentRuntime::is_stop_phrase("ABORT"));
}

TEST_CASE("Spanish stop phrases", "[agent][stop_phrases]") {
    CHECK(AgentRuntime::is_stop_phrase("para"));
    CHECK(AgentRuntime::is_stop_phrase("detente"));
    CHECK(AgentRuntime::is_stop_phrase("basta"));
}

TEST_CASE("German stop phrases", "[agent][stop_phrases]") {
    CHECK(AgentRuntime::is_stop_phrase("stopp"));
    CHECK(AgentRuntime::is_stop_phrase("halt"));
    CHECK(AgentRuntime::is_stop_phrase("hoer auf"));
}

TEST_CASE("Non-stop phrases are rejected", "[agent][stop_phrases]") {
    CHECK_FALSE(AgentRuntime::is_stop_phrase("hello"));
    CHECK_FALSE(AgentRuntime::is_stop_phrase("please continue"));
    CHECK_FALSE(AgentRuntime::is_stop_phrase("what is the capital of France?"));
    CHECK_FALSE(AgentRuntime::is_stop_phrase(""));
    CHECK_FALSE(AgentRuntime::is_stop_phrase("stopping"));
    CHECK_FALSE(AgentRuntime::is_stop_phrase("unstoppable"));
}
