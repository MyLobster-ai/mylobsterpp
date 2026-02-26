#pragma once

#include "openclaw/gateway/protocol.hpp"
#include "openclaw/memory/manager.hpp"

namespace openclaw::gateway {

/// Registers memory.store, memory.recall, memory.search, memory.delete,
/// memory.list, memory.clear, memory.stats, memory.embed,
/// memory.index.rebuild, memory.rag.query handlers on the protocol.
void register_memory_handlers(Protocol& protocol,
                              memory::MemoryManager& memory);

} // namespace openclaw::gateway
