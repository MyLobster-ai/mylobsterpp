#include <catch2/catch_test_macros.hpp>
#include "openclaw/agent/runtime.hpp"
#include "openclaw/core/config.hpp"

using namespace openclaw;
using namespace openclaw::agent;

TEST_CASE("AgentRuntime prepend_system_context accumulates", "[agent]") {
    boost::asio::io_context ioc;
    Config config;
    AgentRuntime runtime(ioc, config);

    runtime.prepend_system_context("Context A");
    runtime.prepend_system_context("Context B");

    auto& prepended = runtime.prepended_system_context();
    REQUIRE(prepended.size() == 2);
    CHECK(prepended[0] == "Context A");
    CHECK(prepended[1] == "Context B");
}

TEST_CASE("AgentRuntime append_system_context accumulates", "[agent]") {
    boost::asio::io_context ioc;
    Config config;
    AgentRuntime runtime(ioc, config);

    runtime.append_system_context("Footer 1");
    runtime.append_system_context("Footer 2");
    runtime.append_system_context("Footer 3");

    auto& appended = runtime.appended_system_context();
    REQUIRE(appended.size() == 3);
    CHECK(appended[0] == "Footer 1");
    CHECK(appended[2] == "Footer 3");
}

TEST_CASE("AgentRuntime clear_system_context_injections clears both", "[agent]") {
    boost::asio::io_context ioc;
    Config config;
    AgentRuntime runtime(ioc, config);

    runtime.prepend_system_context("Header");
    runtime.append_system_context("Footer");

    CHECK(runtime.prepended_system_context().size() == 1);
    CHECK(runtime.appended_system_context().size() == 1);

    runtime.clear_system_context_injections();

    CHECK(runtime.prepended_system_context().empty());
    CHECK(runtime.appended_system_context().empty());
}

TEST_CASE("AgentRuntime context injections start empty", "[agent]") {
    boost::asio::io_context ioc;
    Config config;
    AgentRuntime runtime(ioc, config);

    CHECK(runtime.prepended_system_context().empty());
    CHECK(runtime.appended_system_context().empty());
}

TEST_CASE("AgentRuntime cooldown_window defaults to unknown", "[agent]") {
    boost::asio::io_context ioc;
    Config config;
    AgentRuntime runtime(ioc, config);

    CHECK(runtime.cooldown_window == "unknown");
}

TEST_CASE("AgentRuntime cooldown_window is mutable", "[agent]") {
    boost::asio::io_context ioc;
    Config config;
    AgentRuntime runtime(ioc, config);

    runtime.cooldown_window = "rate_limit";
    CHECK(runtime.cooldown_window == "rate_limit");

    runtime.cooldown_window = "server_error";
    CHECK(runtime.cooldown_window == "server_error");
}

TEST_CASE("AgentRuntime clear clears tool state and context independently", "[agent]") {
    boost::asio::io_context ioc;
    Config config;
    AgentRuntime runtime(ioc, config);

    runtime.prepend_system_context("Plugin header");
    runtime.append_system_context("Plugin footer");

    // clear_pending_tool_state should NOT affect context injections
    runtime.clear_pending_tool_state();
    CHECK(runtime.prepended_system_context().size() == 1);
    CHECK(runtime.appended_system_context().size() == 1);

    // clear_system_context_injections should NOT affect tool state
    runtime.clear_system_context_injections();
    CHECK_FALSE(runtime.has_pending_tool_call());
}

TEST_CASE("AgentRuntime cancel_run and system prompt are independent", "[agent]") {
    boost::asio::io_context ioc;
    Config config;
    AgentRuntime runtime(ioc, config);

    runtime.set_system_prompt("You are helpful.");
    runtime.set_thinking_mode("extended");
    runtime.set_model("claude-opus-4-6");
    runtime.cancel_run("run-123");

    // State should be preserved after cancel
    CHECK(runtime.system_prompt() == "You are helpful.");
    CHECK(runtime.thinking_mode() == "extended");
    CHECK(runtime.model_override() == "claude-opus-4-6");
}
