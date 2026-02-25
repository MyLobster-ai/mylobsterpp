#pragma once

#include <string>
#include <string_view>

namespace openclaw::infra {

/// Chat type classification for heartbeat delivery decisions.
enum class ChatType {
    Direct,   // One-on-one DM
    Group,    // Group chat
    Channel,  // Broadcast channel
    Unknown,  // Cannot determine
};

/// Infers the chat type from a Telegram chat/target identifier.
/// Negative IDs starting with -100 are channels/supergroups;
/// other negative IDs are groups; positive IDs are DMs.
auto infer_telegram_target_chat_type(std::string_view target) -> ChatType;

/// Infers the chat type from a Discord channel/target identifier.
/// DM channels are identified by context (no guild prefix).
auto infer_discord_target_chat_type(std::string_view target, bool is_dm = false) -> ChatType;

/// Infers the chat type from a Slack channel/target identifier.
/// Channels starting with "D" are DMs; "C" are channels; "G" are groups.
auto infer_slack_target_chat_type(std::string_view target) -> ChatType;

/// Infers the chat type from a WhatsApp target identifier.
/// Group JIDs contain "@g.us"; individual JIDs contain "@s.whatsapp.net".
auto infer_whatsapp_target_chat_type(std::string_view target) -> ChatType;

/// Infers the chat type from a Signal target identifier.
/// Group IDs are base64-encoded and typically longer; phone numbers are DMs.
auto infer_signal_target_chat_type(std::string_view target) -> ChatType;

/// Resolves the chat type for a given channel type and target.
auto resolve_heartbeat_delivery_chat_type(std::string_view channel_type,
                                           std::string_view target,
                                           bool is_dm_hint = false) -> ChatType;

/// Returns true if heartbeat delivery should be blocked for this target.
/// DM delivery is blocked by default in v2026.2.24 to prevent unwanted
/// heartbeat messages being sent to individual users' DM channels.
auto should_block_heartbeat_dm(ChatType chat_type) -> bool;

} // namespace openclaw::infra
