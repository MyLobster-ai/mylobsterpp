#pragma once

#include <optional>
#include <regex>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace openclaw::routing {

using json = nlohmann::json;

/// Binding scope for routing rules.
enum class BindingScope {
    Peer,    // Match only specific peer
    Guild,   // Match within a guild/server
    Team,    // Match within a team
    Global,  // Match any context
};

/// Context for scope-aware routing.
struct BindingContext {
    std::string peer_id;
    std::optional<std::string> guild_id;
    std::optional<std::string> team_id;
};

struct IncomingMessage {
    std::string channel;
    std::string sender_id;
    std::string text;
    json metadata;
    std::optional<BindingContext> binding;  // Scope context for routing
};

void to_json(json& j, const IncomingMessage& m);
void from_json(const json& j, IncomingMessage& m);

class RoutingRule {
public:
    virtual ~RoutingRule() = default;

    [[nodiscard]] virtual auto matches(const IncomingMessage& msg) const -> bool = 0;
    [[nodiscard]] virtual auto priority() const -> int { return 0; }
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;
};

class PrefixRule : public RoutingRule {
public:
    PrefixRule(std::string prefix, int priority = 0);

    [[nodiscard]] auto matches(const IncomingMessage& msg) const -> bool override;
    [[nodiscard]] auto priority() const -> int override;
    [[nodiscard]] auto name() const -> std::string_view override;

private:
    std::string prefix_;
    int priority_;
    std::string name_;
};

class RegexRule : public RoutingRule {
public:
    RegexRule(std::string pattern, int priority = 0);

    [[nodiscard]] auto matches(const IncomingMessage& msg) const -> bool override;
    [[nodiscard]] auto priority() const -> int override;
    [[nodiscard]] auto name() const -> std::string_view override;

private:
    std::string pattern_str_;
    std::regex pattern_;
    int priority_;
    std::string name_;
};

class ChannelRule : public RoutingRule {
public:
    ChannelRule(std::string channel, int priority = 0);

    [[nodiscard]] auto matches(const IncomingMessage& msg) const -> bool override;
    [[nodiscard]] auto priority() const -> int override;
    [[nodiscard]] auto name() const -> std::string_view override;

private:
    std::string channel_;
    int priority_;
    std::string name_;
};

/// Scope-aware routing rule that enforces binding scope matching.
class ScopeRule : public RoutingRule {
public:
    ScopeRule(BindingScope scope, std::string target_id, int priority = 0);

    [[nodiscard]] auto matches(const IncomingMessage& msg) const -> bool override;
    [[nodiscard]] auto priority() const -> int override;
    [[nodiscard]] auto name() const -> std::string_view override;

private:
    BindingScope scope_;
    std::string target_id_;
    int priority_;
    std::string name_;
};

} // namespace openclaw::routing
