#pragma once

#include "openclaw/gateway/protocol.hpp"
#include "openclaw/gateway/server.hpp"

namespace openclaw::gateway {

/// Registers node.list, node.describe, node.invoke, node.invoke.result,
/// node.event, node.rename, node.pair.request, node.pair.list,
/// node.pair.approve, node.pair.reject, node.pair.verify,
/// node.canvas.capability.refresh handlers.
void register_nodes_handlers(Protocol& protocol, GatewayServer& server);

} // namespace openclaw::gateway
