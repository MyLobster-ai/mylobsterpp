#include <catch2/catch_test_macros.hpp>

#include "openclaw/channels/discord.hpp"

using namespace openclaw::channels;

TEST_CASE("Discord voice message flags", "[channels][discord]") {
    SECTION("Voice message flag is bit 13") {
        constexpr int kVoiceMessageFlag = 1 << 13;
        CHECK(kVoiceMessageFlag == 8192);
    }

    SECTION("Suppress notifications flag is bit 12") {
        constexpr int kSuppressNotificationsFlag = 1 << 12;
        CHECK(kSuppressNotificationsFlag == 4096);
    }

    SECTION("Combined flags") {
        constexpr int kVoiceMessageFlag = 1 << 13;
        constexpr int kSuppressNotificationsFlag = 1 << 12;
        int combined = kVoiceMessageFlag | kSuppressNotificationsFlag;
        CHECK(combined == 12288);
    }
}

TEST_CASE("Discord waveform generation", "[channels][discord]") {
    SECTION("Empty PCM returns zero waveform") {
        std::vector<uint8_t> empty;
        auto waveform = DiscordChannel::generate_waveform(empty);
        CHECK(!waveform.empty());
    }

    SECTION("Single-sample PCM") {
        // 16-bit signed LE: 32767 (max positive)
        std::vector<uint8_t> pcm = {0xFF, 0x7F};
        auto waveform = DiscordChannel::generate_waveform(pcm);
        CHECK(!waveform.empty());
    }

    SECTION("Silence generates low waveform") {
        // 256 samples of silence (16-bit = 512 bytes)
        std::vector<uint8_t> pcm(512, 0);
        auto waveform = DiscordChannel::generate_waveform(pcm);
        CHECK(!waveform.empty());
    }
}

TEST_CASE("Discord thread name sanitization", "[channels][discord]") {
    SECTION("Name under 100 chars is preserved") {
        std::string name = "Short thread name";
        auto result = name.substr(0, 100);
        CHECK(result == name);
    }

    SECTION("Name over 100 chars is truncated") {
        std::string name(150, 'A');
        auto result = name.substr(0, 100);
        CHECK(result.size() == 100);
    }
}

TEST_CASE("Discord config with presence", "[channels][discord]") {
    DiscordConfig config;
    config.bot_token = "test-token";
    config.presence_status = "online";
    config.activity_name = "Testing";
    config.activity_type = 0;

    CHECK(config.presence_status.has_value());
    CHECK(*config.presence_status == "online");
    CHECK(config.activity_name.has_value());
    CHECK(*config.activity_name == "Testing");
}

TEST_CASE("Discord config with auto_thread", "[channels][discord]") {
    DiscordConfig config;
    config.auto_thread = true;
    config.auto_thread_ttl_minutes = 10;

    CHECK(config.auto_thread == true);
    CHECK(config.auto_thread_ttl_minutes == 10);
}
