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

/// v2026.3.2: Normalize streamed alias/case tool names against allowed set.
/// Preserves whitespace-only streamed placeholders.
auto ToolRegistry::normalize_tool_name(std::string_view raw_name) const -> std::string {
    auto name = std::string(raw_name);

    // Preserve whitespace-only placeholders
    bool all_whitespace = true;
    for (char c : name) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            all_whitespace = false;
            break;
        }
    }
    if (all_whitespace && !name.empty()) {
        return name;
    }

    // Exact match
    if (tools_.contains(name)) {
        return name;
    }

    // Case-insensitive search
    std::string lower_name;
    lower_name.reserve(name.size());
    for (unsigned char c : name) {
        lower_name += static_cast<char>(std::tolower(c));
    }

    for (const auto& [registered_name, _] : tools_) {
        std::string lower_reg;
        lower_reg.reserve(registered_name.size());
        for (unsigned char c : registered_name) {
            lower_reg += static_cast<char>(std::tolower(c));
        }
        if (lower_name == lower_reg) {
            LOG_DEBUG("Tool name normalized: '{}' -> '{}'", raw_name, registered_name);
            return registered_name;
        }
    }

    // Underscore/hyphen normalization
    auto normalize_separators = [](const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            result += (c == '-') ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return result;
    };

    auto normalized = normalize_separators(name);
    for (const auto& [registered_name, _] : tools_) {
        if (normalize_separators(registered_name) == normalized) {
            LOG_DEBUG("Tool name normalized (separators): '{}' -> '{}'", raw_name, registered_name);
            return registered_name;
        }
    }

    return name;  // Return as-is if no match found
}

auto ToolRegistry::execute(std::string_view name, json params)
    -> boost::asio::awaitable<Result<json>> {

    // v2026.3.2: Normalize tool name before lookup
    auto normalized_name = normalize_tool_name(name);
    auto* tool = get(normalized_name);
    if (!tool) {
        co_return make_fail(make_error(
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
