#pragma once

#include "openclaw/gateway/protocol.hpp"

namespace openclaw::gateway {

/// Registers skills.status, skills.bins, skills.install,
/// skills.update handlers.
void register_skills_handlers(Protocol& protocol);

} // namespace openclaw::gateway
