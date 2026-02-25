#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace openclaw::routing {

/// Metadata captured from the originating turn of a conversation.
/// Used to pin reply routing to the original channel/target, preventing
/// cross-channel reply routing attacks where mutable session metadata
/// could redirect replies to unintended channels.
struct TurnSourceMetadata {
    std::optional<std::string> channel;     // originating channel type (e.g. "telegram")
    std::optional<std::string> to;          // target recipient ID
    std::optional<std::string> account_id;  // channel account ID
    std::optional<std::string> thread_id;   // thread context
};

/// Resolves the provider/channel from turn-source metadata.
/// Returns the turn-source channel if present, otherwise falls back to the session channel.
auto resolve_origin_message_provider(const TurnSourceMetadata& turn_source,
                                      std::string_view session_channel) -> std::string;

/// Resolves the target recipient from turn-source metadata.
/// Returns the turn-source target if present, otherwise falls back to the session target.
auto resolve_origin_to(const TurnSourceMetadata& turn_source,
                        std::string_view session_to) -> std::string;

/// Resolves the account ID from turn-source metadata.
/// Returns the turn-source account if present, otherwise falls back to the session account.
auto resolve_origin_account_id(const TurnSourceMetadata& turn_source,
                                std::string_view session_account_id) -> std::string;

} // namespace openclaw::routing
