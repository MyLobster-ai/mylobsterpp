#pragma once

#include "openclaw/gateway/protocol.hpp"
#include "openclaw/gateway/server.hpp"

namespace openclaw::gateway {

/// Registers gateway.info, gateway.ping, gateway.status, gateway.methods,
/// gateway.subscribe, gateway.unsubscribe, gateway.shutdown, gateway.reload,
/// gateway.metrics, gateway.logs handlers on the protocol.
void register_gateway_handlers(Protocol& protocol, GatewayServer& server);

} // namespace openclaw::gateway
