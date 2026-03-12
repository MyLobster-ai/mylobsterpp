#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "openclaw/core/config.hpp"

using namespace openclaw;
using json = nlohmann::json;

TEST_CASE("CompactionConfig JSON round-trip", "[config]") {
    CompactionConfig config;
    config.model = "claude-haiku-4-5";
    config.recent_turns_preserve = 5;
    config.post_compaction_sections = {{"summary", "key_facts"}};
    config.quality_audit_enabled = false;
    config.pre_check_skip_idle = true;

    json j = config;
    auto decoded = j.get<CompactionConfig>();
    CHECK(decoded.model == "claude-haiku-4-5");
    CHECK(decoded.recent_turns_preserve == 5);
    CHECK(decoded.post_compaction_sections->size() == 2);
    CHECK_FALSE(decoded.quality_audit_enabled);
    CHECK(decoded.pre_check_skip_idle);
}

TEST_CASE("WebSearchConfig JSON round-trip", "[config]") {
    WebSearchConfig config;
    config.provider = "brave";
    config.brave_mode = "llm-context";
    config.language = "en";

    json j = config;
    auto decoded = j.get<WebSearchConfig>();
    CHECK(decoded.provider == "brave");
    CHECK(decoded.brave_mode == "llm-context");
    CHECK(decoded.language == "en");
}

TEST_CASE("TalkConfig JSON round-trip", "[config]") {
    TalkConfig config;
    config.silence_timeout_ms = 5000;
    config.tts_provider = "openai";
    config.tts_base_url = "https://custom-tts.example.com";

    json j = config;
    auto decoded = j.get<TalkConfig>();
    CHECK(decoded.silence_timeout_ms == 5000);
    CHECK(decoded.tts_provider == "openai");
    CHECK(decoded.tts_base_url == "https://custom-tts.example.com");
}

TEST_CASE("MemoryConfig with multimodal fields", "[config]") {
    MemoryConfig config;
    config.multimodal_image_indexing = true;
    config.multimodal_audio_indexing = true;
    config.embedding_provider = "gemini";
    config.embedding_dimensions = 256;

    json j = config;
    auto decoded = j.get<MemoryConfig>();
    CHECK(decoded.multimodal_image_indexing);
    CHECK(decoded.multimodal_audio_indexing);
    CHECK(decoded.embedding_provider == "gemini");
    CHECK(decoded.embedding_dimensions == 256);
}

TEST_CASE("AudioConfig defaults", "[config]") {
    AudioConfig config;
    CHECK_FALSE(config.echo_transcript);
    CHECK(config.echo_format == "text");
}

TEST_CASE("CliBannerConfig defaults", "[config]") {
    CliBannerConfig config;
    CHECK(config.tagline_mode == "random");
}
