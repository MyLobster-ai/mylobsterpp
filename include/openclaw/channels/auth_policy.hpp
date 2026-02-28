#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "openclaw/core/logger.hpp"

namespace openclaw::channels {

/// v2026.2.26: Centralized channel authorization policy.
/// Extracted from Telegram's inline DM/group auth logic and shared across
/// Telegram, Discord, and Slack channels.
struct ChannelAuthPolicy {
    /// DM authorization mode: "open" (allow all), "allowlist" (check list),
    /// "pairing" (require pairing flow).
    std::string dm_policy = "open";

    /// Allowlist of sender IDs authorized for DMs.
    std::vector<std::string> dm_allowlist;

    /// Allowlist of group/guild/channel IDs. Empty means all allowed.
    std::vector<std::string> group_allowlist;

    /// Check if a DM sender is authorized.
    [[nodiscard]] auto is_dm_authorized(std::string_view sender_id) const -> bool {
        if (dm_policy == "open") {
            return true;
        }
        if (dm_policy == "allowlist") {
            for (const auto& id : dm_allowlist) {
                if (id == sender_id) {
                    return true;
                }
            }
            return false;
        }
        // "pairing" or unknown → deny
        return false;
    }

    /// Check if a group chat is authorized.
    /// Empty group_allowlist means all groups are allowed.
    [[nodiscard]] auto is_group_authorized(std::string_view group_id) const -> bool {
        if (group_allowlist.empty()) {
            return true;
        }
        for (const auto& id : group_allowlist) {
            if (id == group_id) {
                return true;
            }
        }
        return false;
    }

    /// Combined event authorization.
    /// Checks DM auth for private chats (non-negative chat IDs)
    /// and group auth for group chats (negative chat IDs or guild IDs).
    [[nodiscard]] auto authorize_event(
        std::string_view sender_id,
        std::string_view chat_id,
        std::string_view event_type,
        std::string_view channel_name) const -> bool
    {
        // Private/DM chat check
        if (!chat_id.empty() && !chat_id.starts_with("-")) {
            if (!is_dm_authorized(sender_id)) {
                LOG_DEBUG("[{}] Event '{}' from {} blocked by dm_policy",
                          channel_name, event_type, sender_id);
                return false;
            }
        }
        // Group chat check
        if (!chat_id.empty() && chat_id.starts_with("-")) {
            if (!is_group_authorized(chat_id)) {
                LOG_DEBUG("[{}] Event '{}' in group {} blocked by group_allowlist",
                          channel_name, event_type, chat_id);
                return false;
            }
        }
        return true;
    }
};

} // namespace openclaw::channels
