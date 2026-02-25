#include "openclaw/gateway/talk_config.hpp"

namespace openclaw::gateway {

void to_json(json& j, const TalkProviderConfig& c) {
    j = json::object();
    if (c.voice_id) j["voice_id"] = *c.voice_id;
    if (c.voice_aliases) j["voice_aliases"] = *c.voice_aliases;
    if (c.model_id) j["model_id"] = *c.model_id;
    if (c.output_format) j["output_format"] = *c.output_format;
    if (c.api_key) j["api_key"] = *c.api_key;
}

void from_json(const json& j, TalkProviderConfig& c) {
    if (j.contains("voice_id") && !j["voice_id"].is_null())
        c.voice_id = j["voice_id"].get<std::string>();
    if (j.contains("voice_aliases") && j["voice_aliases"].is_array())
        c.voice_aliases = j["voice_aliases"].get<std::vector<std::string>>();
    if (j.contains("model_id") && !j["model_id"].is_null())
        c.model_id = j["model_id"].get<std::string>();
    if (j.contains("output_format") && !j["output_format"].is_null())
        c.output_format = j["output_format"].get<std::string>();
    if (j.contains("api_key") && !j["api_key"].is_null())
        c.api_key = j["api_key"].get<std::string>();
}

void to_json(json& j, const TalkConfig& c) {
    j = json::object();
    if (c.provider) j["provider"] = *c.provider;
    if (!c.providers.empty()) {
        j["providers"] = json::object();
        for (const auto& [name, cfg] : c.providers) {
            j["providers"][name] = cfg;
        }
    }
    if (c.voice_id) j["voice_id"] = *c.voice_id;
    if (c.model_id) j["model_id"] = *c.model_id;
    if (c.api_key) j["api_key"] = *c.api_key;
}

void from_json(const json& j, TalkConfig& c) {
    if (j.contains("provider") && !j["provider"].is_null())
        c.provider = j["provider"].get<std::string>();
    if (j.contains("providers") && j["providers"].is_object()) {
        for (auto& [key, val] : j["providers"].items()) {
            c.providers[key] = val.get<TalkProviderConfig>();
        }
    }
    if (j.contains("voice_id") && !j["voice_id"].is_null())
        c.voice_id = j["voice_id"].get<std::string>();
    if (j.contains("model_id") && !j["model_id"].is_null())
        c.model_id = j["model_id"].get<std::string>();
    if (j.contains("api_key") && !j["api_key"].is_null())
        c.api_key = j["api_key"].get<std::string>();
}

auto normalize_talk_config(TalkConfig& config) -> void {
    // If no providers are configured, migrate legacy flat fields to elevenlabs
    if (config.providers.empty() &&
        (config.voice_id || config.model_id || config.api_key)) {
        TalkProviderConfig legacy;
        legacy.voice_id = config.voice_id;
        legacy.model_id = config.model_id;
        legacy.api_key = config.api_key;
        config.providers[std::string(kDefaultTalkProvider)] = std::move(legacy);
    }
}

auto resolve_active_talk_provider(const TalkConfig& config)
    -> std::optional<std::pair<std::string, TalkProviderConfig>> {
    if (config.providers.empty()) return std::nullopt;

    // Explicit provider selection
    if (config.provider) {
        auto it = config.providers.find(*config.provider);
        if (it != config.providers.end()) {
            return *it;
        }
    }

    // Single-provider inference
    if (config.providers.size() == 1) {
        return *config.providers.begin();
    }

    // Default to elevenlabs if present
    auto it = config.providers.find(std::string(kDefaultTalkProvider));
    if (it != config.providers.end()) {
        return *it;
    }

    return std::nullopt;
}

auto build_talk_config_response(const TalkConfig& config) -> json {
    json response = config;

    // Merge active provider into top-level for backward compatibility
    auto active = resolve_active_talk_provider(config);
    if (active) {
        auto& [name, provider_config] = *active;
        response["active_provider"] = name;
        if (provider_config.voice_id)
            response["voice_id"] = *provider_config.voice_id;
        if (provider_config.model_id)
            response["model_id"] = *provider_config.model_id;
        if (provider_config.api_key)
            response["api_key"] = *provider_config.api_key;
    }

    return response;
}

} // namespace openclaw::gateway
