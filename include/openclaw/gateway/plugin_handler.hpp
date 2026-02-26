#pragma once

#include "openclaw/gateway/protocol.hpp"
#include "openclaw/plugins/loader.hpp"

namespace openclaw::gateway {

/// Registers plugin.list, plugin.install, plugin.uninstall,
/// plugin.enable, plugin.disable, plugin.configure,
/// plugin.call, plugin.status handlers.
void register_plugin_handlers(Protocol& protocol,
                              plugins::PluginLoader& plugins);

} // namespace openclaw::gateway
