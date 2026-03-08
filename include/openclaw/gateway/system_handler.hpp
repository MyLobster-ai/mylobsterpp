#pragma once

#include "openclaw/gateway/protocol.hpp"
#include "openclaw/gateway/server.hpp"

namespace openclaw::gateway {

/// Registers system-level methods: last-heartbeat, set-heartbeats,
/// system-presence, system-event, health, status, send,
/// update.run, secrets.reload, secrets.resolve, doctor.memory.status,
/// logs.tail, push.test, talk.config, talk.mode, tools.catalog,
/// web.login.start, web.login.wait, voicewake.get, voicewake.set handlers.
void register_system_handlers(Protocol& protocol, GatewayServer& server);

} // namespace openclaw::gateway
