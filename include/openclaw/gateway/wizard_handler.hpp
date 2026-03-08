#pragma once

#include "openclaw/gateway/protocol.hpp"

namespace openclaw::gateway {

/// Registers wizard.start, wizard.next, wizard.cancel,
/// wizard.status handlers.
void register_wizard_handlers(Protocol& protocol);

} // namespace openclaw::gateway
