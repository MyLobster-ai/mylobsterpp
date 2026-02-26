#include "openclaw/infra/outbound_channel.hpp"

#include "openclaw/core/logger.hpp"

#include <cctype>
#include <sstream>

namespace openclaw::infra {

void OutboundChannelResolver::register_channel(std::string name, std::string plugin_id) {
    auto normalized = normalize_name(name);
    LOG_DEBUG("Registering outbound channel '{}' -> plugin '{}'", normalized, plugin_id);
    channels_[std::move(normalized)] = std::move(plugin_id);
}

auto OutboundChannelResolver::unregister_channel(std::string_view name) -> bool {
    auto normalized = normalize_name(name);
    return channels_.erase(normalized) > 0;
}

auto OutboundChannelResolver::resolve_outbound_channel_plugin(std::string_view name) const
    -> Result<std::string>
{
    auto normalized = normalize_name(name);

    if (normalized.empty()) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Empty channel name for outbound resolution"));
    }

    auto it = channels_.find(normalized);
    if (it != channels_.end()) {
        return it->second;
    }

    // Build actionable error with available channels
    std::ostringstream available;
    available << "Channel '" << normalized << "' not found. Available channels: [";
    bool first = true;
    for (const auto& [ch_name, _] : channels_) {
        if (!first) available << ", ";
        available << ch_name;
        first = false;
    }
    available << "]";

    return std::unexpected(make_error(
        ErrorCode::NotFound,
        "Outbound channel plugin not found",
        available.str()));
}

auto OutboundChannelResolver::has_channel(std::string_view name) const -> bool {
    auto normalized = normalize_name(name);
    return channels_.contains(normalized);
}

auto OutboundChannelResolver::normalize_name(std::string_view name) -> std::string {
    std::string result;
    result.reserve(name.size());

    // Trim leading whitespace
    auto start = name.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return {};

    // Trim trailing whitespace
    auto end = name.find_last_not_of(" \t\r\n");
    auto trimmed = name.substr(start, end - start + 1);

    // Lowercase
    for (char c : trimmed) {
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return result;
}

} // namespace openclaw::infra
