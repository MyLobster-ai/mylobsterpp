#include <catch2/catch_test_macros.hpp>

#include "openclaw/gateway/talk_config.hpp"

using namespace openclaw::gateway;
using json = nlohmann::json;

TEST_CASE("TalkProviderConfig JSON round-trip", "[gateway][talk_config]") {
    TalkProviderConfig config;
    config.voice_id = "voice-123";
    config.voice_aliases = std::vector<std::string>{"alice", "bob"};
    config.model_id = "eleven_multilingual_v2";
    config.output_format = "mp3_44100_128";
    config.api_key = "sk-test";

    json j = config;
    CHECK(j["voice_id"] == "voice-123");
    CHECK(j["voice_aliases"].size() == 2);
    CHECK(j["model_id"] == "eleven_multilingual_v2");
    CHECK(j["output_format"] == "mp3_44100_128");
    CHECK(j["api_key"] == "sk-test");

    auto roundtrip = j.get<TalkProviderConfig>();
    CHECK(roundtrip.voice_id == config.voice_id);
    CHECK(roundtrip.voice_aliases == config.voice_aliases);
    CHECK(roundtrip.model_id == config.model_id);
    CHECK(roundtrip.output_format == config.output_format);
    CHECK(roundtrip.api_key == config.api_key);
}

TEST_CASE("TalkProviderConfig optional fields omitted in JSON", "[gateway][talk_config]") {
    TalkProviderConfig config;
    json j = config;
    CHECK_FALSE(j.contains("voice_id"));
    CHECK_FALSE(j.contains("model_id"));
    CHECK_FALSE(j.contains("api_key"));
    CHECK_FALSE(j.contains("voice_aliases"));
    CHECK_FALSE(j.contains("output_format"));
}

TEST_CASE("TalkConfig JSON round-trip with providers", "[gateway][talk_config]") {
    TalkConfig config;
    config.provider = "elevenlabs";
    config.providers["elevenlabs"] = TalkProviderConfig{.voice_id = "v1"};
    config.providers["playht"] = TalkProviderConfig{.voice_id = "v2"};

    json j = config;
    CHECK(j["provider"] == "elevenlabs");
    CHECK(j["providers"]["elevenlabs"]["voice_id"] == "v1");
    CHECK(j["providers"]["playht"]["voice_id"] == "v2");

    auto roundtrip = j.get<TalkConfig>();
    CHECK(roundtrip.provider == "elevenlabs");
    CHECK(roundtrip.providers.size() == 2);
}

TEST_CASE("TalkConfig legacy flat fields", "[gateway][talk_config]") {
    TalkConfig config;
    config.voice_id = "legacy-voice";
    config.model_id = "legacy-model";
    config.api_key = "legacy-key";

    json j = config;
    CHECK(j["voice_id"] == "legacy-voice");
    CHECK(j["model_id"] == "legacy-model");
    CHECK(j["api_key"] == "legacy-key");
}

TEST_CASE("normalize_talk_config migrates legacy to elevenlabs", "[gateway][talk_config]") {
    TalkConfig config;
    config.voice_id = "legacy-voice";
    config.model_id = "legacy-model";
    config.api_key = "legacy-key";

    normalize_talk_config(config);

    REQUIRE(config.providers.size() == 1);
    CHECK(config.providers.count("elevenlabs") == 1);
    CHECK(config.providers["elevenlabs"].voice_id == "legacy-voice");
    CHECK(config.providers["elevenlabs"].model_id == "legacy-model");
    CHECK(config.providers["elevenlabs"].api_key == "legacy-key");
}

TEST_CASE("normalize_talk_config does not overwrite existing providers", "[gateway][talk_config]") {
    TalkConfig config;
    config.voice_id = "legacy";
    config.providers["playht"] = TalkProviderConfig{.voice_id = "ph-voice"};

    normalize_talk_config(config);

    // Should NOT migrate because providers already exist
    CHECK(config.providers.size() == 1);
    CHECK(config.providers.count("playht") == 1);
    CHECK(config.providers.count("elevenlabs") == 0);
}

TEST_CASE("resolve_active_talk_provider explicit selection", "[gateway][talk_config]") {
    TalkConfig config;
    config.provider = "playht";
    config.providers["elevenlabs"] = TalkProviderConfig{.voice_id = "v1"};
    config.providers["playht"] = TalkProviderConfig{.voice_id = "v2"};

    auto result = resolve_active_talk_provider(config);
    REQUIRE(result.has_value());
    CHECK(result->first == "playht");
    CHECK(result->second.voice_id == "v2");
}

TEST_CASE("resolve_active_talk_provider single-provider inference", "[gateway][talk_config]") {
    TalkConfig config;
    config.providers["playht"] = TalkProviderConfig{.voice_id = "v2"};

    auto result = resolve_active_talk_provider(config);
    REQUIRE(result.has_value());
    CHECK(result->first == "playht");
}

TEST_CASE("resolve_active_talk_provider defaults to elevenlabs", "[gateway][talk_config]") {
    TalkConfig config;
    config.providers["elevenlabs"] = TalkProviderConfig{.voice_id = "v1"};
    config.providers["playht"] = TalkProviderConfig{.voice_id = "v2"};

    auto result = resolve_active_talk_provider(config);
    REQUIRE(result.has_value());
    CHECK(result->first == "elevenlabs");
}

TEST_CASE("resolve_active_talk_provider returns nullopt for empty", "[gateway][talk_config]") {
    TalkConfig config;
    auto result = resolve_active_talk_provider(config);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("build_talk_config_response merges active provider", "[gateway][talk_config]") {
    TalkConfig config;
    config.providers["elevenlabs"] = TalkProviderConfig{
        .voice_id = "v1",
        .model_id = "m1",
        .api_key = "k1",
    };

    auto response = build_talk_config_response(config);
    CHECK(response["active_provider"] == "elevenlabs");
    CHECK(response["voice_id"] == "v1");
    CHECK(response["model_id"] == "m1");
    CHECK(response["api_key"] == "k1");
}

TEST_CASE("kDefaultTalkProvider is elevenlabs", "[gateway][talk_config]") {
    CHECK(kDefaultTalkProvider == "elevenlabs");
}
