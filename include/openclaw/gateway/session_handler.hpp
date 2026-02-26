#pragma once

#include "openclaw/gateway/protocol.hpp"
#include "openclaw/sessions/manager.hpp"

namespace openclaw::gateway {

/// Registers session.create, session.get, session.list, session.destroy,
/// session.heartbeat, session.update, session.context.*, session.history
/// handlers on the protocol.
void register_session_handlers(Protocol& protocol,
                               sessions::SessionManager& sessions);

} // namespace openclaw::gateway
