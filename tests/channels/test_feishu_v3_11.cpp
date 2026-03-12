#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "openclaw/channels/feishu.hpp"

using namespace openclaw::channels;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// FeishuConfig comprehensive coverage
// ---------------------------------------------------------------------------

TEST_CASE("FeishuConfig group_broadcast default false", "[channels][feishu]") {
    FeishuConfig config;
    config.app_id = "cli_test";
    config.app_secret = "secret";
    CHECK_FALSE(config.group_broadcast);
}

TEST_CASE("FeishuConfig group_broadcast enabled", "[channels][feishu]") {
    FeishuConfig config;
    config.app_id = "cli_test";
    config.app_secret = "secret";
    config.group_broadcast = true;
    CHECK(config.group_broadcast);
}

TEST_CASE("FeishuConfig observer_session_isolation default true", "[channels][feishu]") {
    FeishuConfig config;
    config.app_id = "cli_test";
    config.app_secret = "secret";
    CHECK(config.observer_session_isolation);
}

TEST_CASE("FeishuConfig observer_session_isolation disabled", "[channels][feishu]") {
    FeishuConfig config;
    config.app_id = "cli_test";
    config.app_secret = "secret";
    config.observer_session_isolation = false;
    CHECK_FALSE(config.observer_session_isolation);
}

TEST_CASE("FeishuConfig native_markdown_tables default true", "[channels][feishu]") {
    FeishuConfig config;
    config.app_id = "cli_test";
    config.app_secret = "secret";
    CHECK(config.native_markdown_tables);
}

TEST_CASE("FeishuConfig verification_token and encrypt_key", "[channels][feishu]") {
    FeishuConfig config;
    config.app_id = "cli_test";
    config.app_secret = "secret";

    SECTION("defaults to nullopt") {
        CHECK_FALSE(config.verification_token.has_value());
        CHECK_FALSE(config.encrypt_key.has_value());
    }
    SECTION("set values") {
        config.verification_token = "verify-123";
        config.encrypt_key = "key-456";
        CHECK(*config.verification_token == "verify-123");
        CHECK(*config.encrypt_key == "key-456");
    }
}

// ---------------------------------------------------------------------------
// FeishuChannel::markdown_to_card
// ---------------------------------------------------------------------------

TEST_CASE("markdown_to_card returns valid card structure", "[channels][feishu]") {
    auto card = FeishuChannel::markdown_to_card("Hello world");
    REQUIRE(card.is_object());
    CHECK(card.contains("config"));
    CHECK(card.contains("elements"));
    CHECK(card["config"]["wide_screen_mode"] == true);
}

TEST_CASE("markdown_to_card wraps content in lark_md tag", "[channels][feishu]") {
    auto card = FeishuChannel::markdown_to_card("| A | B |\n|---|---|\n| 1 | 2 |");
    REQUIRE(card["elements"].is_array());
    REQUIRE(card["elements"].size() == 1);

    auto& element = card["elements"][0];
    CHECK(element["tag"] == "div");
    CHECK(element["text"]["tag"] == "lark_md");
    CHECK(element["text"]["content"] == "| A | B |\n|---|---|\n| 1 | 2 |");
}

TEST_CASE("markdown_to_card with empty string", "[channels][feishu]") {
    auto card = FeishuChannel::markdown_to_card("");
    REQUIRE(card.is_object());
    CHECK(card["elements"][0]["text"]["content"] == "");
}

TEST_CASE("markdown_to_card with multiline content", "[channels][feishu]") {
    std::string md = "# Heading\n\nParagraph with **bold** text.\n\n- Item 1\n- Item 2";
    auto card = FeishuChannel::markdown_to_card(md);
    CHECK(card["elements"][0]["text"]["content"] == md);
}

// ---------------------------------------------------------------------------
// make_feishu_channel factory
// ---------------------------------------------------------------------------

TEST_CASE("make_feishu_channel creates channel from valid settings", "[channels][feishu]") {
    boost::asio::io_context ioc;
    json settings = {
        {"app_id", "cli_test123"},
        {"app_secret", "secret456"},
        {"channel_name", "my-feishu"},
        {"group_broadcast", true},
        {"observer_session_isolation", false},
        {"native_markdown_tables", true},
    };

    auto channel = make_feishu_channel(settings, ioc);
    REQUIRE(channel != nullptr);
    CHECK(channel->name() == "my-feishu");
    CHECK(channel->type() == "feishu");
}

TEST_CASE("make_feishu_channel returns nullptr for missing credentials", "[channels][feishu]") {
    boost::asio::io_context ioc;

    SECTION("missing app_id") {
        json settings = {{"app_secret", "secret"}};
        auto channel = make_feishu_channel(settings, ioc);
        CHECK(channel == nullptr);
    }
    SECTION("missing app_secret") {
        json settings = {{"app_id", "id"}};
        auto channel = make_feishu_channel(settings, ioc);
        CHECK(channel == nullptr);
    }
    SECTION("both missing") {
        json settings = json::object();
        auto channel = make_feishu_channel(settings, ioc);
        CHECK(channel == nullptr);
    }
}

TEST_CASE("make_feishu_channel default config values", "[channels][feishu]") {
    boost::asio::io_context ioc;
    json settings = {
        {"app_id", "cli_test"},
        {"app_secret", "secret"},
    };

    auto channel = make_feishu_channel(settings, ioc);
    REQUIRE(channel != nullptr);
    CHECK(channel->name() == "feishu");  // default channel name
}

TEST_CASE("make_feishu_channel with verification_token", "[channels][feishu]") {
    boost::asio::io_context ioc;
    json settings = {
        {"app_id", "cli_test"},
        {"app_secret", "secret"},
        {"verification_token", "v-token"},
        {"encrypt_key", "e-key"},
    };

    auto channel = make_feishu_channel(settings, ioc);
    REQUIRE(channel != nullptr);
}
