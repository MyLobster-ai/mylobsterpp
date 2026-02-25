#include <catch2/catch_test_macros.hpp>

#include "openclaw/core/config.hpp"
#include "openclaw/infra/heartbeat.hpp"

using namespace openclaw::infra;

TEST_CASE("HeartbeatConfig default target is 'none'", "[heartbeat]") {
    openclaw::HeartbeatConfig config;
    CHECK(config.target == "none");
}

// ---------------------------------------------------------------------------
// Telegram chat type inference
// ---------------------------------------------------------------------------

TEST_CASE("Telegram: positive IDs are DMs", "[heartbeat][telegram]") {
    CHECK(infer_telegram_target_chat_type("12345678") == ChatType::Direct);
    CHECK(infer_telegram_target_chat_type("1") == ChatType::Direct);
}

TEST_CASE("Telegram: -100 prefix is channel", "[heartbeat][telegram]") {
    CHECK(infer_telegram_target_chat_type("-1001234567890") == ChatType::Channel);
}

TEST_CASE("Telegram: negative non-100 IDs are groups", "[heartbeat][telegram]") {
    CHECK(infer_telegram_target_chat_type("-987654321") == ChatType::Group);
}

TEST_CASE("Telegram: @username targets are channels", "[heartbeat][telegram]") {
    CHECK(infer_telegram_target_chat_type("@mychannel") == ChatType::Channel);
}

TEST_CASE("Telegram: empty target is unknown", "[heartbeat][telegram]") {
    CHECK(infer_telegram_target_chat_type("") == ChatType::Unknown);
}

// ---------------------------------------------------------------------------
// Discord chat type inference
// ---------------------------------------------------------------------------

TEST_CASE("Discord: is_dm hint classifies DMs", "[heartbeat][discord]") {
    CHECK(infer_discord_target_chat_type("123456", true) == ChatType::Direct);
    CHECK(infer_discord_target_chat_type("123456", false) == ChatType::Channel);
}

// ---------------------------------------------------------------------------
// Slack chat type inference
// ---------------------------------------------------------------------------

TEST_CASE("Slack: D prefix is DM", "[heartbeat][slack]") {
    CHECK(infer_slack_target_chat_type("D024BE91L") == ChatType::Direct);
}

TEST_CASE("Slack: C prefix is channel", "[heartbeat][slack]") {
    CHECK(infer_slack_target_chat_type("C024BE91L") == ChatType::Channel);
}

TEST_CASE("Slack: G prefix is group", "[heartbeat][slack]") {
    CHECK(infer_slack_target_chat_type("G024BE91L") == ChatType::Group);
}

// ---------------------------------------------------------------------------
// WhatsApp chat type inference
// ---------------------------------------------------------------------------

TEST_CASE("WhatsApp: @g.us is group", "[heartbeat][whatsapp]") {
    CHECK(infer_whatsapp_target_chat_type("120363025@g.us") == ChatType::Group);
}

TEST_CASE("WhatsApp: @s.whatsapp.net is DM", "[heartbeat][whatsapp]") {
    CHECK(infer_whatsapp_target_chat_type("14155552671@s.whatsapp.net") == ChatType::Direct);
}

TEST_CASE("WhatsApp: @broadcast is channel", "[heartbeat][whatsapp]") {
    CHECK(infer_whatsapp_target_chat_type("status@broadcast") == ChatType::Channel);
}

// ---------------------------------------------------------------------------
// Signal chat type inference
// ---------------------------------------------------------------------------

TEST_CASE("Signal: phone number is DM", "[heartbeat][signal]") {
    CHECK(infer_signal_target_chat_type("+14155552671") == ChatType::Direct);
}

TEST_CASE("Signal: long base64 string is group", "[heartbeat][signal]") {
    CHECK(infer_signal_target_chat_type("bPkHfj4/rG3MQnRp7K2xa0YzNjs=") == ChatType::Group);
}

// ---------------------------------------------------------------------------
// DM blocking
// ---------------------------------------------------------------------------

TEST_CASE("DM delivery is blocked by default", "[heartbeat]") {
    CHECK(should_block_heartbeat_dm(ChatType::Direct) == true);
    CHECK(should_block_heartbeat_dm(ChatType::Group) == false);
    CHECK(should_block_heartbeat_dm(ChatType::Channel) == false);
    CHECK(should_block_heartbeat_dm(ChatType::Unknown) == false);
}

// ---------------------------------------------------------------------------
// resolve_heartbeat_delivery_chat_type
// ---------------------------------------------------------------------------

TEST_CASE("Resolve delegates to correct channel", "[heartbeat]") {
    CHECK(resolve_heartbeat_delivery_chat_type("telegram", "12345") == ChatType::Direct);
    CHECK(resolve_heartbeat_delivery_chat_type("slack", "D0001") == ChatType::Direct);
    CHECK(resolve_heartbeat_delivery_chat_type("whatsapp", "123@g.us") == ChatType::Group);
    CHECK(resolve_heartbeat_delivery_chat_type("unknown_channel", "xxx") == ChatType::Unknown);
}
