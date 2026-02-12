#include "openclaw/agent/tool_registry.hpp"

#include "openclaw/core/logger.hpp"

namespace openclaw::agent {

void ToolRegistry::register_tool(std::unique_ptr<Tool> tool) {
    if (!tool) {
        LOG_WARN("Attempted to register a null tool");
        return;
    }

    auto def = tool->definition();
    auto name = def.name;

    if (tools_.contains(name)) {
        LOG_WARN("Replacing existing tool: {}", name);
    } else {
        LOG_INFO("Registered tool: {}", name);
    }

    tools_[std::move(name)] = std::move(tool);
}

auto ToolRegistry::get(std::string_view name) -> Tool* {
    auto it = tools_.find(std::string(name));
    if (it != tools_.end()) {
        return it->second.get();
    }
    return nullptr;
}

auto ToolRegistry::get(std::string_view name) const -> const Tool* {
    auto it = tools_.find(std::string(name));
    if (it != tools_.end()) {
        return it->second.get();
    }
    return nullptr;
}

auto ToolRegistry::list() const -> std::vector<ToolDefinition> {
    std::vector<ToolDefinition> defs;
    defs.reserve(tools_.size());

    for (const auto& [name, tool] : tools_) {
        defs.push_back(tool->definition());
    }

    return defs;
}

auto ToolRegistry::to_json() const -> std::vector<json> {
    std::vector<json> result;
    result.reserve(tools_.size());

    for (const auto& [name, tool] : tools_) {
        result.push_back(tool->definition().to_json());
    }

    return result;
}

auto ToolRegistry::to_anthropic_json() const -> std::vector<json> {
    std::vector<json> result;
    result.reserve(tools_.size());

    for (const auto& [name, tool] : tools_) {
        result.push_back(tool->definition().to_anthropic_json());
    }

    return result;
}

auto ToolRegistry::to_openai_json() const -> std::vector<json> {
    std::vector<json> result;
    result.reserve(tools_.size());

    for (const auto& [name, tool] : tools_) {
        result.push_back(tool->definition().to_openai_json());
    }

    return result;
}

auto ToolRegistry::execute(std::string_view name, json params)
    -> boost::asio::awaitable<Result<json>> {

    auto* tool = get(name);
    if (!tool) {
        co_return std::unexpected(make_error(
            ErrorCode::NotFound,
            "Tool not found",
            std::string(name)));
    }

    LOG_DEBUG("Executing tool: {} with params: {}", name, params.dump());

    auto result = co_await tool->execute(std::move(params));

    if (result.has_value()) {
        LOG_DEBUG("Tool {} executed successfully", name);
    } else {
        LOG_WARN("Tool {} execution failed: {}", name, result.error().what());
    }

    co_return result;
}

auto ToolRegistry::size() const noexcept -> std::size_t {
    return tools_.size();
}

auto ToolRegistry::contains(std::string_view name) const -> bool {
    return tools_.contains(std::string(name));
}

auto ToolRegistry::remove(std::string_view name) -> bool {
    auto it = tools_.find(std::string(name));
    if (it != tools_.end()) {
        LOG_INFO("Removed tool: {}", name);
        tools_.erase(it);
        return true;
    }
    return false;
}

void ToolRegistry::clear() {
    LOG_INFO("Clearing all {} registered tools", tools_.size());
    tools_.clear();
}

} // namespace openclaw::agent
