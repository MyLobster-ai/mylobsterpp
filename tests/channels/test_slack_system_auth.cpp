#include <catch2/catch_test_macros.hpp>

#include "openclaw/infra/heartbeat.hpp"

using namespace openclaw::infra;

// Test the v2026.2.25 DirectPolicy heartbeat delivery changes

TEST_CASE("Heartbeat delivery: Allow policy permits DM", "[channels][heartbeat]") {
    CHECK_FALSE(should_block_heartbeat_delivery(
        ChatType::Direct, DirectPolicy::Allow));
}

TEST_CASE("Heartbeat delivery: Block policy blocks DM", "[channels][heartbeat]") {
    CHECK(should_block_heartbeat_delivery(
        ChatType::Direct, DirectPolicy::Block));
}

TEST_CASE("Heartbeat delivery: group always allowed regardless of policy", "[channels][heartbeat]") {
    CHECK_FALSE(should_block_heartbeat_delivery(
        ChatType::Group, DirectPolicy::Block));
    CHECK_FALSE(should_block_heartbeat_delivery(
        ChatType::Group, DirectPolicy::Allow));
}

TEST_CASE("Heartbeat delivery: channel always allowed regardless of policy", "[channels][heartbeat]") {
    CHECK_FALSE(should_block_heartbeat_delivery(
        ChatType::Channel, DirectPolicy::Block));
    CHECK_FALSE(should_block_heartbeat_delivery(
        ChatType::Channel, DirectPolicy::Allow));
}

TEST_CASE("Heartbeat delivery: agent override takes precedence", "[channels][heartbeat]") {
    // Global Allow, agent overrides to Block
    CHECK(should_block_heartbeat_delivery(
        ChatType::Direct, DirectPolicy::Allow, DirectPolicy::Block));

    // Global Block, agent overrides to Allow
    CHECK_FALSE(should_block_heartbeat_delivery(
        ChatType::Direct, DirectPolicy::Block, DirectPolicy::Allow));
}

TEST_CASE("Heartbeat delivery: default policy is Allow (v2026.2.25)", "[channels][heartbeat]") {
    // Default policy changed from Block to Allow in v2026.2.25
    CHECK_FALSE(should_block_heartbeat_delivery(ChatType::Direct));
}

TEST_CASE("Slack DM routing: D-prefix channel IDs are DMs", "[channels][slack]") {
    CHECK(infer_slack_target_chat_type("D0123456789") == ChatType::Direct);
    CHECK(infer_slack_target_chat_type("DABCDEF") == ChatType::Direct);
}

TEST_CASE("Slack channel routing: C-prefix are channels", "[channels][slack]") {
    CHECK(infer_slack_target_chat_type("C0123456789") == ChatType::Channel);
}

TEST_CASE("Slack group routing: G-prefix are groups", "[channels][slack]") {
    CHECK(infer_slack_target_chat_type("G0123456789") == ChatType::Group);
}
