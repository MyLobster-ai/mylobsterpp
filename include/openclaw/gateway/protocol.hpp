#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"
#include "openclaw/gateway/frame.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

/// Signature for an RPC method handler.
/// Receives params as JSON, returns result as JSON.
using MethodHandler = std::function<awaitable<json>(json params)>;

/// Metadata about a registered RPC method.
struct MethodInfo {
    std::string name;
    std::string description;
    std::string group;
};

/// The Protocol class manages method registration, discovery, and dispatch.
/// It maintains a registry of named RPC methods, each with a handler function,
/// and routes incoming RequestFrames to the appropriate handler.
class Protocol {
public:
    Protocol();

    /// Register a method handler.
    void register_method(std::string name, MethodHandler handler,
                         std::string description = "",
                         std::string group = "");

    /// Check whether a method is registered.
    [[nodiscard]] auto has_method(std::string_view name) const -> bool;

    /// List all registered method names.
    [[nodiscard]] auto methods() const -> std::vector<MethodInfo>;

    /// List method names belonging to a specific group.
    [[nodiscard]] auto methods_in_group(std::string_view group) const
        -> std::vector<MethodInfo>;

    /// Dispatch a request to the matching handler.
    /// Returns an error if the method is not found.
    auto dispatch(const RequestFrame& request) -> awaitable<Result<json>>;

    /// Register all built-in method stubs.
    /// These are placeholder implementations that return
    /// "not implemented" until wired to real subsystems.
    void register_builtins();

private:
    struct Entry {
        MethodHandler handler;
        MethodInfo info;
    };

    std::unordered_map<std::string, Entry> methods_;

    // Helpers for registering grouped stubs.
    void register_gateway_methods();
    void register_session_methods();
    void register_channel_methods();
    void register_tool_methods();
    void register_memory_methods();
    void register_browser_methods();
    void register_provider_methods();
    void register_plugin_methods();
    void register_agent_methods();
    void register_cron_methods();
    void register_config_methods();
};

} // namespace openclaw::gateway
