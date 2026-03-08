#pragma once

#include "openclaw/gateway/protocol.hpp"

namespace openclaw::gateway {

/// Registers exec.approval.request, exec.approval.resolve,
/// exec.approval.waitDecision, exec.approvals.get, exec.approvals.set,
/// exec.approvals.node.get, exec.approvals.node.set handlers.
void register_exec_approval_handlers(Protocol& protocol);

} // namespace openclaw::gateway
