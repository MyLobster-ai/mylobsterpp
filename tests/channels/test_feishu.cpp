#include <catch2/catch_test_macros.hpp>
#include "openclaw/channels/feishu.hpp"

using namespace openclaw::channels;

TEST_CASE("FeishuConfig defaults", "[channels][feishu]") {
    FeishuConfig config;
    config.app_id = "cli_test";
    config.app_secret = "secret";

    CHECK(config.channel_name == "feishu");
    CHECK_FALSE(config.group_broadcast);
    CHECK(config.observer_session_isolation);
    CHECK(config.native_markdown_tables);
}

TEST_CASE("FeishuChannel::markdown_to_card static helper", "[channels][feishu]") {
    auto card = FeishuChannel::markdown_to_card("| A | B |\n|---|---|\n| 1 | 2 |");
    CHECK(card.is_object());
}
