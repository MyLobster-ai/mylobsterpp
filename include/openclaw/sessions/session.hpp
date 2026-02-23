#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "openclaw/core/types.hpp"

namespace openclaw::sessions {

using json = nlohmann::json;

enum class SessionState {
    Active,
    Idle,
    Closed,
};

NLOHMANN_JSON_SERIALIZE_ENUM(SessionState, {
    {SessionState::Active, "active"},
    {SessionState::Idle, "idle"},
    {SessionState::Closed, "closed"},
})

struct SessionData {
    Session session;
    SessionState state = SessionState::Active;
    json metadata;
    int auto_compaction_count = 0;  // Only incremented on completed compactions
};

void to_json(json& j, const SessionData& d);
void from_json(const json& j, SessionData& d);

/// Redacts credentials (API keys, tokens, secrets, passwords) from session text.
/// Replaces matching values with "***REDACTED***".
auto redact_credentials(std::string_view text) -> std::string;

/// Strips inbound metadata blocks (<!-- metadata:...-->) from text.
auto strip_inbound_metadata(std::string_view text) -> std::string;

} // namespace openclaw::sessions
