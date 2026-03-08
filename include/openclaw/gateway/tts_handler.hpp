#pragma once

#include "openclaw/gateway/protocol.hpp"

namespace openclaw::gateway {

/// Registers tts.status, tts.enable, tts.disable, tts.convert,
/// tts.setProvider, tts.providers handlers.
void register_tts_handlers(Protocol& protocol);

} // namespace openclaw::gateway
