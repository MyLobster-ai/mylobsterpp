#include "openclaw/infra/heartbeat.hpp"

#include <algorithm>
#include <cctype>

namespace openclaw::infra {

auto infer_telegram_target_chat_type(std::string_view target) -> ChatType {
    if (target.empty()) return ChatType::Unknown;

    // Numeric chat IDs: negative = group/channel, positive = DM
    if (target[0] == '-') {
        // -100... prefix indicates a channel or supergroup
        if (target.starts_with("-100")) return ChatType::Channel;
        return ChatType::Group;
    }

    // Positive numeric IDs are DMs
    bool all_digits = std::all_of(target.begin(), target.end(),
                                   [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
    if (all_digits && !target.empty()) return ChatType::Direct;

    // @username targets are typically channels or groups
    if (target.starts_with("@")) return ChatType::Channel;

    return ChatType::Unknown;
}

auto infer_discord_target_chat_type(std::string_view target, bool is_dm) -> ChatType {
    if (target.empty()) return ChatType::Unknown;
    if (is_dm) return ChatType::Direct;
    // Discord doesn't encode DM vs channel in the ID itself;
    // the is_dm hint must be provided by the caller
    return ChatType::Channel;
}

auto infer_slack_target_chat_type(std::string_view target) -> ChatType {
    if (target.empty()) return ChatType::Unknown;

    // Slack channel ID prefixes: D = DM, C = channel, G = group
    char prefix = target[0];
    switch (prefix) {
        case 'D': return ChatType::Direct;
        case 'C': return ChatType::Channel;
        case 'G': return ChatType::Group;
        default: return ChatType::Unknown;
    }
}

auto infer_whatsapp_target_chat_type(std::string_view target) -> ChatType {
    if (target.empty()) return ChatType::Unknown;

    if (target.find("@g.us") != std::string_view::npos) return ChatType::Group;
    if (target.find("@s.whatsapp.net") != std::string_view::npos) return ChatType::Direct;
    if (target.find("@broadcast") != std::string_view::npos) return ChatType::Channel;

    return ChatType::Unknown;
}

auto infer_signal_target_chat_type(std::string_view target) -> ChatType {
    if (target.empty()) return ChatType::Unknown;

    // Signal group IDs are typically base64-encoded (longer, contain = or /)
    // Phone numbers start with + and are shorter
    if (target.starts_with("+")) return ChatType::Direct;
    if (target.size() > 20) return ChatType::Group;  // base64 group ID

    return ChatType::Unknown;
}

auto resolve_heartbeat_delivery_chat_type(std::string_view channel_type,
                                           std::string_view target,
                                           bool is_dm_hint) -> ChatType {
    if (channel_type == "telegram") return infer_telegram_target_chat_type(target);
    if (channel_type == "discord") return infer_discord_target_chat_type(target, is_dm_hint);
    if (channel_type == "slack") return infer_slack_target_chat_type(target);
    if (channel_type == "whatsapp") return infer_whatsapp_target_chat_type(target);
    if (channel_type == "signal") return infer_signal_target_chat_type(target);
    return ChatType::Unknown;
}

auto should_block_heartbeat_dm(ChatType chat_type) -> bool {
    return chat_type == ChatType::Direct;
}

} // namespace openclaw::infra
