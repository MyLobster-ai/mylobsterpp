#include <catch2/catch_test_macros.hpp>

#include "openclaw/sessions/manager.hpp"
#include "openclaw/sessions/session.hpp"

using namespace openclaw::sessions;

TEST_CASE("Session fork overflow: below threshold passes", "[sessions][fork]") {
    SessionForkConfig config{.parent_fork_max_tokens = 100000};
    CHECK_FALSE(should_skip_parent_fork(50000, config));
}

TEST_CASE("Session fork overflow: at threshold passes", "[sessions][fork]") {
    SessionForkConfig config{.parent_fork_max_tokens = 100000};
    CHECK_FALSE(should_skip_parent_fork(100000, config));
}

TEST_CASE("Session fork overflow: above threshold triggers skip", "[sessions][fork]") {
    SessionForkConfig config{.parent_fork_max_tokens = 100000};
    CHECK(should_skip_parent_fork(100001, config));
}

TEST_CASE("Session fork overflow: custom threshold", "[sessions][fork]") {
    SessionForkConfig config{.parent_fork_max_tokens = 50000};
    CHECK_FALSE(should_skip_parent_fork(49999, config));
    CHECK_FALSE(should_skip_parent_fork(50000, config));
    CHECK(should_skip_parent_fork(50001, config));
}

TEST_CASE("Session fork overflow: default config", "[sessions][fork]") {
    // Default is 100000
    CHECK_FALSE(should_skip_parent_fork(99999));
    CHECK(should_skip_parent_fork(100001));
}

TEST_CASE("Model identity: provider/model format", "[sessions][model]") {
    auto ref = resolve_session_model_identity_ref("anthropic/claude-sonnet-4-6");
    CHECK(ref.provider == "anthropic");
    CHECK(ref.model == "claude-sonnet-4-6");
}

TEST_CASE("Model identity: provider:model format", "[sessions][model]") {
    auto ref = resolve_session_model_identity_ref("openai:gpt-4o");
    CHECK(ref.provider == "openai");
    CHECK(ref.model == "gpt-4o");
}

TEST_CASE("Model identity: claude- prefix inference", "[sessions][model]") {
    auto ref = resolve_session_model_identity_ref("claude-sonnet-4-6");
    CHECK(ref.provider == "anthropic");
    CHECK(ref.model == "claude-sonnet-4-6");
}

TEST_CASE("Model identity: gpt- prefix inference", "[sessions][model]") {
    auto ref = resolve_session_model_identity_ref("gpt-4o");
    CHECK(ref.provider == "openai");
    CHECK(ref.model == "gpt-4o");
}

TEST_CASE("Model identity: o1- prefix inference", "[sessions][model]") {
    auto ref = resolve_session_model_identity_ref("o1-preview");
    CHECK(ref.provider == "openai");
    CHECK(ref.model == "o1-preview");
}

TEST_CASE("Model identity: gemini- prefix inference", "[sessions][model]") {
    auto ref = resolve_session_model_identity_ref("gemini-2.0-flash");
    CHECK(ref.provider == "gemini");
    CHECK(ref.model == "gemini-2.0-flash");
}

TEST_CASE("Model identity: mistral- prefix inference", "[sessions][model]") {
    auto ref = resolve_session_model_identity_ref("mistral-large-latest");
    CHECK(ref.provider == "mistral");
    CHECK(ref.model == "mistral-large-latest");
}

TEST_CASE("Model identity: unknown prefix", "[sessions][model]") {
    auto ref = resolve_session_model_identity_ref("custom-model-v1");
    CHECK(ref.provider == "unknown");
    CHECK(ref.model == "custom-model-v1");
}
