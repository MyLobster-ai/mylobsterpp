#pragma once

#include <string>

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
};

void to_json(json& j, const SessionData& d);
void from_json(const json& j, SessionData& d);

} // namespace openclaw::sessions
