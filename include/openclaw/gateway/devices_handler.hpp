#pragma once

#include "openclaw/gateway/protocol.hpp"

namespace openclaw::gateway {

/// Registers device.pair.list, device.pair.approve, device.pair.reject,
/// device.pair.remove, device.token.rotate, device.token.revoke handlers.
void register_devices_handlers(Protocol& protocol);

} // namespace openclaw::gateway
