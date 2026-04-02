#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/channels/matrix.hpp"

using namespace openclaw::channels;
using json = nlohmann::json;

TEST_CASE("MatrixConfig v2026.3.31 enhancements", "[channels][matrix]") {
    MatrixConfig config;

    SECTION("new fields have correct defaults") {
        CHECK(!config.history_limit.has_value());
        CHECK(!config.proxy.has_value());
        CHECK(config.draft_streaming == false);
        CHECK(config.thread_replies.is_null());
    }

    SECTION("history_limit can be set") {
        config.history_limit = 50;
        CHECK(*config.history_limit == 50);
    }

    SECTION("proxy can be set") {
        config.proxy = "http://proxy.example.com:8080";
        CHECK(*config.proxy == "http://proxy.example.com:8080");
    }

    SECTION("draft_streaming can be enabled") {
        config.draft_streaming = true;
        CHECK(config.draft_streaming);
    }

    SECTION("thread_replies accepts JSON config") {
        config.thread_replies = json{
            {"!room1:example.com", {{"threadReplies", true}}},
            {"!room2:example.com", {{"threadReplies", false}}},
        };
        CHECK(config.thread_replies.size() == 2);
    }
}

TEST_CASE("MatrixConfig v2026.4.1 features", "[channels][matrix]") {
    MatrixConfig config;

    SECTION("allow_bots defaults to nullopt") {
        CHECK(!config.allow_bots.has_value());
    }

    SECTION("allow_bots accepts valid values") {
        config.allow_bots = "mentions";
        CHECK(*config.allow_bots == "mentions");
    }

    SECTION("allow_private_network defaults to false") {
        CHECK(config.allow_private_network == false);
    }
}

TEST_CASE("make_matrix_channel factory (v2026.3.31)", "[channels][matrix]") {
    boost::asio::io_context ioc;

    SECTION("factory parses new config fields") {
        json settings = {
            {"homeserver_url", "https://matrix.example.com"},
            {"access_token", "syt_test_token"},
            {"history_limit", 100},
            {"proxy", "http://proxy:8080"},
            {"draft_streaming", true},
            {"thread_replies", {{"!room:example.com", {{"threadReplies", true}}}}},
            {"allow_bots", "all"},
            {"allow_private_network", true},
        };
        auto ch = make_matrix_channel(settings, ioc);
        REQUIRE(ch != nullptr);
        CHECK(ch->type() == "matrix");
    }

    SECTION("factory works with minimal config") {
        json settings = {
            {"homeserver_url", "https://matrix.example.com"},
            {"access_token", "syt_test_token"},
        };
        auto ch = make_matrix_channel(settings, ioc);
        REQUIRE(ch != nullptr);
    }
}
