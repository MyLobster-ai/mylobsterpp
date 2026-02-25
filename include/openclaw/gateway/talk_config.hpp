#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace openclaw::gateway {

using json = nlohmann::json;

/// Default talk provider name (ElevenLabs).
constexpr std::string_view kDefaultTalkProvider = "elevenlabs";

/// Per-provider talk configuration.
struct TalkProviderConfig {
    std::optional<std::string> voice_id;
    std::optional<std::vector<std::string>> voice_aliases;
    std::optional<std::string> model_id;
    std::optional<std::string> output_format;
    std::optional<std::string> api_key;
};

void to_json(json& j, const TalkProviderConfig& c);
void from_json(const json& j, TalkProviderConfig& c);

/// Talk configuration supporting multiple providers.
struct TalkConfig {
    std::optional<std::string> provider;  // active provider ID
    std::map<std::string, TalkProviderConfig> providers;

    // Legacy flat fields (for backward compatibility)
    std::optional<std::string> voice_id;
    std::optional<std::string> model_id;
    std::optional<std::string> api_key;
};

void to_json(json& j, const TalkConfig& c);
void from_json(const json& j, TalkConfig& c);

/// Normalizes a TalkConfig, migrating legacy flat fields into
/// the providers map (populating elevenlabs if no providers exist).
auto normalize_talk_config(TalkConfig& config) -> void;

/// Resolves the active talk provider config.
/// Uses explicit `provider` field, or infers from single-provider configs.
auto resolve_active_talk_provider(const TalkConfig& config)
    -> std::optional<std::pair<std::string, TalkProviderConfig>>;

/// Builds a backward-compatible response by merging active provider config
/// into top-level fields for legacy clients.
auto build_talk_config_response(const TalkConfig& config) -> json;

} // namespace openclaw::gateway
