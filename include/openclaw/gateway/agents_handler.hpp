#pragma once

#include "openclaw/gateway/protocol.hpp"
#include "openclaw/gateway/server.hpp"

namespace openclaw::gateway {

/// Registers agents.list, agents.create, agents.update, agents.delete,
/// agents.files.list, agents.files.get, agents.files.set,
/// agent.identity.get, agent.wait handlers.
void register_agents_handlers(Protocol& protocol, GatewayServer& server);

} // namespace openclaw::gateway
