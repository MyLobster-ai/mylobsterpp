#include "openclaw/routing/turn_source.hpp"

namespace openclaw::routing {

auto resolve_origin_message_provider(const TurnSourceMetadata& turn_source,
                                      std::string_view session_channel) -> std::string {
    if (turn_source.channel.has_value() && !turn_source.channel->empty()) {
        return *turn_source.channel;
    }
    return std::string(session_channel);
}

auto resolve_origin_to(const TurnSourceMetadata& turn_source,
                        std::string_view session_to) -> std::string {
    if (turn_source.to.has_value() && !turn_source.to->empty()) {
        return *turn_source.to;
    }
    return std::string(session_to);
}

auto resolve_origin_account_id(const TurnSourceMetadata& turn_source,
                                std::string_view session_account_id) -> std::string {
    if (turn_source.account_id.has_value() && !turn_source.account_id->empty()) {
        return *turn_source.account_id;
    }
    return std::string(session_account_id);
}

} // namespace openclaw::routing
