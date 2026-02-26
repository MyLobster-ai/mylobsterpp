#pragma once

#include "openclaw/agent/tool_registry.hpp"
#include "openclaw/gateway/protocol.hpp"
#include "openclaw/gateway/server.hpp"

namespace openclaw::gateway {

/// Registers tool.list, tool.execute, tool.register, tool.unregister,
/// tool.describe, tool.enable, tool.disable, tool.shell.exec,
/// tool.file.read, tool.file.write, tool.file.list, tool.file.search,
/// tool.http.request, tool.code.run, tool.code.analyze handlers.
void register_tool_handlers(Protocol& protocol,
                            GatewayServer& server,
                            agent::ToolRegistry& tools);

} // namespace openclaw::gateway
