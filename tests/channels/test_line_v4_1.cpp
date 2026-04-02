#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/channels/line.hpp"

using namespace openclaw::channels;
using json = nlohmann::json;

TEST_CASE("LineConfig v2026.3.31 outbound media defaults", "[channels][line]") {
    LineConfig config;
    config.channel_access_token = "test-token";
    config.channel_secret = "test-secret";

    SECTION("media send flags default to true") {
        CHECK(config.enable_image_send == true);
        CHECK(config.enable_video_send == true);
        CHECK(config.enable_audio_send == true);
    }

    SECTION("flags can be disabled") {
        config.enable_image_send = false;
        config.enable_video_send = false;
        config.enable_audio_send = false;
        CHECK_FALSE(config.enable_image_send);
        CHECK_FALSE(config.enable_video_send);
        CHECK_FALSE(config.enable_audio_send);
    }
}

TEST_CASE("LINE make_video_message (v2026.3.31)", "[channels][line]") {
    SECTION("basic video message") {
        auto msg = LineChannel::make_video_message(
            "https://example.com/video.mp4",
            "https://example.com/preview.jpg");
        CHECK(msg["type"] == "video");
        CHECK(msg["originalContentUrl"] == "https://example.com/video.mp4");
        CHECK(msg["previewImageUrl"] == "https://example.com/preview.jpg");
        CHECK_FALSE(msg.contains("trackingId"));
    }

    SECTION("video message with tracking ID") {
        auto msg = LineChannel::make_video_message(
            "https://example.com/video.mp4",
            "https://example.com/preview.jpg",
            "track-123");
        CHECK(msg["trackingId"] == "track-123");
    }
}

TEST_CASE("LINE make_audio_message (v2026.3.31)", "[channels][line]") {
    SECTION("audio message with duration") {
        auto msg = LineChannel::make_audio_message(
            "https://example.com/audio.m4a", 45000);
        CHECK(msg["type"] == "audio");
        CHECK(msg["originalContentUrl"] == "https://example.com/audio.m4a");
        CHECK(msg["duration"] == 45000);
    }

    SECTION("audio message defaults to 60s duration") {
        auto msg = LineChannel::make_audio_message(
            "https://example.com/audio.m4a");
        CHECK(msg["duration"] == 60000);
    }
}

TEST_CASE("LINE make_line_channel factory (v2026.3.31)", "[channels][line]") {
    SECTION("factory parses media send flags") {
        json settings = {
            {"channel_access_token", "test-token"},
            {"channel_secret", "test-secret"},
            {"enable_image_send", false},
            {"enable_video_send", true},
            {"enable_audio_send", false},
        };
        // Factory creates a valid channel (non-null)
        // We can't easily inspect internal config, but we can check it doesn't crash
        // and returns non-null.
        boost::asio::io_context ioc;
        auto ch = make_line_channel(settings, ioc);
        REQUIRE(ch != nullptr);
        CHECK(ch->type() == "line");
    }
}
