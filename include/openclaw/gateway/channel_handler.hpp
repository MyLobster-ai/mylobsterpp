#pragma once

#include "openclaw/channels/registry.hpp"
#include "openclaw/gateway/protocol.hpp"

namespace openclaw::gateway {

/// Registers channel.list, channel.connect, channel.disconnect,
/// channel.status, channel.send, channel.receive, channel.configure,
/// channel.telegram.webhook, channel.discord.setup, channel.slack.setup,
/// channel.whatsapp.setup, channel.sms.send handlers.
void register_channel_handlers(Protocol& protocol,
                               channels::ChannelRegistry& channels);

} // namespace openclaw::gateway
