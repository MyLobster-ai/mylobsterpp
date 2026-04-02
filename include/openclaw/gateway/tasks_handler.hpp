#pragma once

#include "openclaw/gateway/protocol.hpp"
#include "openclaw/tasks/task_registry.hpp"

namespace openclaw::gateway {

/// v2026.3.31/v2026.4.1: Register tasks.list, tasks.show, tasks.cancel,
/// tasks.create, tasks.flows.list, tasks.flows.show handlers.
void register_tasks_handlers(Protocol& protocol,
                             tasks::TaskRegistry& registry);

} // namespace openclaw::gateway
