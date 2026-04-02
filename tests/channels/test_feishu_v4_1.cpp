#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/channels/feishu.hpp"

using namespace openclaw::channels;
using json = nlohmann::json;

TEST_CASE("FeishuConfig v2026.4.1 Drive comments", "[channels][feishu]") {
    FeishuConfig config;

    SECTION("drive comment defaults") {
        CHECK(config.enable_drive_comments == false);
        CHECK(config.drive_comment_in_thread == true);
    }

    SECTION("streaming and card header defaults") {
        CHECK(config.enable_reasoning_stream == false);
        CHECK(config.identity_card_headers == false);
    }
}

TEST_CASE("make_feishu_channel factory (v2026.4.1)", "[channels][feishu]") {
    boost::asio::io_context ioc;

    SECTION("factory parses v2026.4.1 fields") {
        json settings = {
            {"app_id", "cli_test"},
            {"app_secret", "secret_test"},
            {"enable_drive_comments", true},
            {"drive_comment_in_thread", false},
            {"enable_reasoning_stream", true},
            {"identity_card_headers", true},
        };
        auto ch = make_feishu_channel(settings, ioc);
        REQUIRE(ch != nullptr);
        CHECK(ch->type() == "feishu");
    }
}
