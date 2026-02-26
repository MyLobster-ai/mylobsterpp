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

/// v2026.2.25: Model identity reference for session model resolution.
/// Supports parsing "provider/model", "provider:model", or inference
/// from known model prefixes (claude- → anthropic, gpt- → openai, etc.).
struct ModelIdentityRef {
    std::string provider;
    std::string model;
};

/// Parses a model string into provider + model components.
///
/// Accepted formats:
///   - "anthropic/claude-sonnet-4-6" → {anthropic, claude-sonnet-4-6}
///   - "openai:gpt-4o" → {openai, gpt-4o}
///   - "claude-sonnet-4-6" → {anthropic, claude-sonnet-4-6} (inferred)
///   - "gpt-4o" → {openai, gpt-4o} (inferred)
///   - "gemini-2.0-flash" → {gemini, gemini-2.0-flash} (inferred)
[[nodiscard]] auto resolve_session_model_identity_ref(std::string_view model_str)
    -> ModelIdentityRef;

void to_json(json& j, const SessionData& d);
void from_json(const json& j, SessionData& d);

/// Redacts credentials (API keys, tokens, secrets, passwords) from session text.
/// Replaces matching values with "***REDACTED***".
auto redact_credentials(std::string_view text) -> std::string;

/// Strips inbound metadata blocks (<!-- metadata:...-->) from text.
auto strip_inbound_metadata(std::string_view text) -> std::string;

} // namespace openclaw::sessions
