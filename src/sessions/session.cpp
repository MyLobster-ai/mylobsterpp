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

} // namespace openclaw::sessions
