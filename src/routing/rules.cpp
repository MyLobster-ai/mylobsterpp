#include "openclaw/routing/rules.hpp"

#include "openclaw/core/logger.hpp"

namespace openclaw::routing {

// -- IncomingMessage serialization --

void to_json(json& j, const IncomingMessage& m) {
    j = json{
        {"channel", m.channel},
        {"sender_id", m.sender_id},
        {"text", m.text},
        {"metadata", m.metadata},
    };
}

void from_json(const json& j, IncomingMessage& m) {
    j.at("channel").get_to(m.channel);
    j.at("sender_id").get_to(m.sender_id);
    j.at("text").get_to(m.text);
    if (j.contains("metadata")) {
        m.metadata = j.at("metadata");
    }
}

// -- PrefixRule --

PrefixRule::PrefixRule(std::string prefix, int priority)
    : prefix_(std::move(prefix))
    , priority_(priority)
    , name_("prefix:" + prefix_) {}

auto PrefixRule::matches(const IncomingMessage& msg) const -> bool {
    return msg.text.starts_with(prefix_);
}

auto PrefixRule::priority() const -> int {
    return priority_;
}

auto PrefixRule::name() const -> std::string_view {
    return name_;
}

// -- RegexRule --

RegexRule::RegexRule(std::string pattern, int priority)
    : pattern_str_(std::move(pattern))
    , pattern_(pattern_str_, std::regex::ECMAScript | std::regex::optimize)
    , priority_(priority)
    , name_("regex:" + pattern_str_) {}

auto RegexRule::matches(const IncomingMessage& msg) const -> bool {
    try {
        return std::regex_search(msg.text, pattern_);
    } catch (const std::regex_error& e) {
        LOG_ERROR("Regex match error for pattern '{}': {}", pattern_str_, e.what());
        return false;
    }
}

auto RegexRule::priority() const -> int {
    return priority_;
}

auto RegexRule::name() const -> std::string_view {
    return name_;
}

// -- ChannelRule --

ChannelRule::ChannelRule(std::string channel, int priority)
    : channel_(std::move(channel))
    , priority_(priority)
    , name_("channel:" + channel_) {}

auto ChannelRule::matches(const IncomingMessage& msg) const -> bool {
    return msg.channel == channel_;
}

auto ChannelRule::priority() const -> int {
    return priority_;
}

auto ChannelRule::name() const -> std::string_view {
    return name_;
}

// -- ScopeRule --

ScopeRule::ScopeRule(BindingScope scope, std::string target_id, int priority)
    : scope_(scope)
    , target_id_(std::move(target_id))
    , priority_(priority) {
    switch (scope_) {
        case BindingScope::Peer:   name_ = "scope:peer:" + target_id_; break;
        case BindingScope::Guild:  name_ = "scope:guild:" + target_id_; break;
        case BindingScope::Team:   name_ = "scope:team:" + target_id_; break;
        case BindingScope::Global: name_ = "scope:global"; break;
    }
}

auto ScopeRule::matches(const IncomingMessage& msg) const -> bool {
    if (scope_ == BindingScope::Global) {
        return true;
    }

    if (!msg.binding) {
        // No binding context; only Global scope matches
        return false;
    }

    const auto& ctx = *msg.binding;
    switch (scope_) {
        case BindingScope::Peer:
            return ctx.peer_id == target_id_;
        case BindingScope::Guild:
            return ctx.guild_id && *ctx.guild_id == target_id_;
        case BindingScope::Team:
            return ctx.team_id && *ctx.team_id == target_id_;
        case BindingScope::Global:
            return true;
    }
    return false;
}

auto ScopeRule::priority() const -> int {
    return priority_;
}

auto ScopeRule::name() const -> std::string_view {
    return name_;
}

} // namespace openclaw::routing
