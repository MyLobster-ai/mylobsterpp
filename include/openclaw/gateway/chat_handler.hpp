#pragma once

#include <memory>

#include "openclaw/agent/runtime.hpp"
#include "openclaw/gateway/protocol.hpp"
#include "openclaw/gateway/server.hpp"
#include "openclaw/sessions/manager.hpp"

namespace openclaw::gateway {

/// Registers chat.send (and agent.chat alias) handlers on the protocol.
/// These are the bridge-critical methods: the Rust bridge calls chat.send
/// for every user message and expects streaming delta/final events back.
void register_chat_handlers(Protocol& protocol,
                            GatewayServer& server,
                            sessions::SessionManager& sessions,
                            agent::AgentRuntime& runtime);

} // namespace openclaw::gateway
