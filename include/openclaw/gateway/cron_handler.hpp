#pragma once

#include "openclaw/cron/scheduler.hpp"
#include "openclaw/gateway/protocol.hpp"

namespace openclaw::gateway {

/// Registers cron.list, cron.create, cron.delete, cron.enable,
/// cron.disable, cron.trigger, cron.status handlers.
void register_cron_handlers(Protocol& protocol,
                            cron::CronScheduler& scheduler);

} // namespace openclaw::gateway
