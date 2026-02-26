#pragma once

#include "openclaw/agent/runtime.hpp"
#include "openclaw/gateway/protocol.hpp"
#include "openclaw/gateway/server.hpp"
#include "openclaw/sessions/manager.hpp"

namespace openclaw::gateway {

/// Registers agent.* handlers (chat aliases, system_prompt, thinking,
/// model, conversation CRUD) on the protocol.
void register_agent_handlers(Protocol& protocol,
                             GatewayServer& server,
                             sessions::SessionManager& sessions,
                             agent::AgentRuntime& runtime);

} // namespace openclaw::gateway
