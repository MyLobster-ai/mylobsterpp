#pragma once

#include "openclaw/gateway/protocol.hpp"
#include "openclaw/sessions/manager.hpp"

namespace openclaw::gateway {

/// Registers usage.status, usage.cost, sessions.usage,
/// sessions.usage.logs, sessions.usage.timeseries, models.list handlers.
void register_usage_handlers(Protocol& protocol,
                             sessions::SessionManager& sessions);

} // namespace openclaw::gateway
