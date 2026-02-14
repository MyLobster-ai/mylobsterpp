#include <catch2/catch_test_macros.hpp>

#include "openclaw/channels/telegram.hpp"

using namespace openclaw::channels;

TEST_CASE("Telegram voice MIME detection", "[channels][telegram]") {
    SECTION("MP3 files are voice-compatible") {
        CHECK(TelegramChannel::is_voice_compatible("audio.mp3"));
        CHECK(TelegramChannel::is_voice_compatible("AUDIO.MP3"));
        CHECK(TelegramChannel::is_voice_compatible("my_recording.mp3"));
    }

    SECTION("M4A files are voice-compatible") {
        CHECK(TelegramChannel::is_voice_compatible("recording.m4a"));
        CHECK(TelegramChannel::is_voice_compatible("voice.M4A"));
    }

    SECTION("OGG files are voice-compatible") {
        CHECK(TelegramChannel::is_voice_compatible("voice.ogg"));
        CHECK(TelegramChannel::is_voice_compatible("audio.oga"));
        CHECK(TelegramChannel::is_voice_compatible("voice.opus"));
    }

    SECTION("Non-audio files are not voice-compatible") {
        CHECK_FALSE(TelegramChannel::is_voice_compatible("document.pdf"));
        CHECK_FALSE(TelegramChannel::is_voice_compatible("image.png"));
        CHECK_FALSE(TelegramChannel::is_voice_compatible("video.mp4"));
        CHECK_FALSE(TelegramChannel::is_voice_compatible("text.txt"));
        CHECK_FALSE(TelegramChannel::is_voice_compatible(""));
    }

    SECTION("WAV files are not voice-compatible") {
        CHECK_FALSE(TelegramChannel::is_voice_compatible("audio.wav"));
        CHECK_FALSE(TelegramChannel::is_voice_compatible("audio.flac"));
    }
}

TEST_CASE("Telegram command menu cap at 100", "[channels][telegram]") {
    SECTION("Under 100 commands pass through") {
        std::vector<std::pair<std::string, std::string>> cmds;
        for (int i = 0; i < 50; ++i) {
            cmds.emplace_back("cmd" + std::to_string(i), "Description " + std::to_string(i));
        }
        auto result = TelegramChannel::build_capped_menu_commands(cmds);
        CHECK(result.size() == 50);
    }

    SECTION("Over 100 commands are capped") {
        std::vector<std::pair<std::string, std::string>> cmds;
        for (int i = 0; i < 150; ++i) {
            cmds.emplace_back("cmd" + std::to_string(i), "Description");
        }
        auto result = TelegramChannel::build_capped_menu_commands(cmds);
        CHECK(result.size() == 100);
    }

    SECTION("Invalid commands are filtered") {
        std::vector<std::pair<std::string, std::string>> cmds = {
            {"valid_cmd", "Valid"},
            {"Invalid-Cmd", "Invalid (has dash)"},
            {"UPPERCASE", "Invalid (uppercase)"},
            {"ok", "Valid"},
            {"too_long_command_name_that_exceeds_the_32_char_limit_for_telegram", "Invalid"},
        };
        auto result = TelegramChannel::build_capped_menu_commands(cmds);
        CHECK(result.size() == 2);
        CHECK(result[0].first == "valid_cmd");
        CHECK(result[1].first == "ok");
    }

    SECTION("Duplicate commands are removed") {
        std::vector<std::pair<std::string, std::string>> cmds = {
            {"start", "Start 1"},
            {"help", "Help"},
            {"start", "Start 2"},
        };
        auto result = TelegramChannel::build_capped_menu_commands(cmds);
        CHECK(result.size() == 2);
    }
}
