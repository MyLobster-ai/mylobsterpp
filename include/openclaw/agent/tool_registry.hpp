#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>

#include "openclaw/agent/tool.hpp"
#include "openclaw/core/error.hpp"

namespace openclaw::agent {

/// Registry that holds all available tools for the agent.
///
/// Tools are registered by name and can be looked up, listed, or
/// executed by name. The registry owns all registered tool instances.
class ToolRegistry {
public:
    ToolRegistry() = default;
    ~ToolRegistry() = default;

    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;
    ToolRegistry(ToolRegistry&&) = default;
    ToolRegistry& operator=(ToolRegistry&&) = default;

    /// Register a tool. The registry takes ownership. If a tool with the
    /// same name already exists, it will be replaced.
    void register_tool(std::unique_ptr<Tool> tool);

    /// Look up a tool by name. Returns nullptr if not found.
    [[nodiscard]] auto get(std::string_view name) -> Tool*;

    /// Look up a tool by name (const). Returns nullptr if not found.
    [[nodiscard]] auto get(std::string_view name) const -> const Tool*;

    /// Return definitions for all registered tools.
    [[nodiscard]] auto list() const -> std::vector<ToolDefinition>;

    /// Return definitions as JSON suitable for a provider API.
    [[nodiscard]] auto to_json() const -> std::vector<json>;

    /// Return definitions in Anthropic tool format.
    [[nodiscard]] auto to_anthropic_json() const -> std::vector<json>;

    /// Return definitions in OpenAI function tool format.
    [[nodiscard]] auto to_openai_json() const -> std::vector<json>;

    /// Execute a tool by name with the given parameters.
    /// Returns an error if the tool is not found.
    auto execute(std::string_view name, json params)
        -> boost::asio::awaitable<Result<json>>;

    /// Return the number of registered tools.
    [[nodiscard]] auto size() const noexcept -> std::size_t;

    /// Check if a tool with the given name is registered.
    [[nodiscard]] auto contains(std::string_view name) const -> bool;

    /// Remove a tool by name. Returns true if the tool was found and removed.
    auto remove(std::string_view name) -> bool;

    /// Remove all registered tools.
    void clear();

private:
    std::unordered_map<std::string, std::unique_ptr<Tool>> tools_;
};

} // namespace openclaw::agent
