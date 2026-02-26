#include "openclaw/sessions/session.hpp"

#include "openclaw/core/utils.hpp"

#include <regex>

namespace openclaw::sessions {

void to_json(json& j, const SessionData& d) {
    j = json{
        {"id", d.session.id},
        {"user_id", d.session.user_id},
        {"device_id", d.session.device_id},
        {"state", d.state},
        {"metadata", d.metadata},
        {"created_at", utils::timestamp_ms()},
    };
    if (d.session.channel) {
        j["channel"] = *d.session.channel;
    }
}

void from_json(const json& j, SessionData& d) {
    j.at("id").get_to(d.session.id);
    j.at("user_id").get_to(d.session.user_id);
    j.at("device_id").get_to(d.session.device_id);
    j.at("state").get_to(d.state);
    if (j.contains("metadata")) {
        d.metadata = j.at("metadata");
    }
    if (j.contains("channel")) {
        d.session.channel = j.at("channel").get<std::string>();
    }
}

auto redact_credentials(std::string_view text) -> std::string {
    std::string result(text);

    // Redact patterns: key=value, "key": "value" for sensitive keys
    static const std::regex credential_patterns[] = {
        std::regex(R"re((api[_-]?key|token|secret|password|bearer|authorization)\s*[=:]\s*"?([a-zA-Z0-9_\-\.\/\+]{8,})"?)re", std::regex::icase),
        std::regex(R"re(Bearer\s+[a-zA-Z0-9_\-\.\/\+]{8,})re", std::regex::icase),
        std::regex(R"re((sk-|pk-|rk-|key-)[a-zA-Z0-9_\-]{16,})re", std::regex::icase),
    };

    for (const auto& pattern : credential_patterns) {
        result = std::regex_replace(result, pattern, "***REDACTED***");
    }

    return result;
}

auto strip_inbound_metadata(std::string_view text) -> std::string {
    std::string result(text);
    static const std::regex metadata_pattern(R"(<!--\s*metadata:.*?-->)", std::regex::icase);
    result = std::regex_replace(result, metadata_pattern, "");
    return result;
}

auto resolve_session_model_identity_ref(std::string_view model_str)
    -> ModelIdentityRef
{
    ModelIdentityRef ref;

    // Try "provider/model" format
    auto slash = model_str.find('/');
    if (slash != std::string_view::npos && slash > 0 && slash < model_str.size() - 1) {
        ref.provider = std::string(model_str.substr(0, slash));
        ref.model = std::string(model_str.substr(slash + 1));
        return ref;
    }

    // Try "provider:model" format
    auto colon = model_str.find(':');
    if (colon != std::string_view::npos && colon > 0 && colon < model_str.size() - 1) {
        ref.provider = std::string(model_str.substr(0, colon));
        ref.model = std::string(model_str.substr(colon + 1));
        return ref;
    }

    // Infer provider from model prefix
    ref.model = std::string(model_str);

    if (model_str.starts_with("claude-") || model_str.starts_with("claude3") ||
        model_str.starts_with("claude_")) {
        ref.provider = "anthropic";
    } else if (model_str.starts_with("gpt-") || model_str.starts_with("o1-") ||
               model_str.starts_with("o3-") || model_str.starts_with("chatgpt-")) {
        ref.provider = "openai";
    } else if (model_str.starts_with("gemini-")) {
        ref.provider = "gemini";
    } else if (model_str.starts_with("mistral-") || model_str.starts_with("mixtral-") ||
               model_str.starts_with("codestral-")) {
        ref.provider = "mistral";
    } else if (model_str.starts_with("llama-") || model_str.starts_with("meta-llama")) {
        ref.provider = "meta";
    } else {
        ref.provider = "unknown";
    }

    return ref;
}

} // namespace openclaw::sessions
