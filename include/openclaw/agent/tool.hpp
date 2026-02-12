#pragma once

#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"

namespace openclaw::agent {

using json = nlohmann::json;
using boost::asio::awaitable;

/// Describes a single parameter for a tool.
struct ToolParameter {
    std::string name;
    std::string type;         // JSON Schema type: "string", "number", "boolean", "object", "array"
    std::string description;
    bool required = true;
    std::optional<json> default_value;
    std::optional<std::vector<std::string>> enum_values;
};

/// Full definition of a tool, used to describe the tool to an AI provider.
struct ToolDefinition {
    std::string name;
    std::string description;
    std::vector<ToolParameter> parameters;

    /// Convert this definition to a JSON object suitable for provider APIs.
    /// The output format follows the JSON Schema standard used by both
    /// Anthropic and OpenAI for tool/function definitions.
    [[nodiscard]] auto to_json() const -> json;

    /// Convert this definition to the Anthropic tool format.
    [[nodiscard]] auto to_anthropic_json() const -> json;

    /// Convert this definition to the OpenAI function tool format.
    [[nodiscard]] auto to_openai_json() const -> json;
};

/// Abstract base class for tools that can be invoked by the agent.
///
/// Each tool provides a definition (name, description, parameters) and
/// an execute method that performs the tool's action asynchronously.
class Tool {
public:
    virtual ~Tool() = default;

    /// Return the tool's definition for registration and provider communication.
    [[nodiscard]] virtual auto definition() const -> ToolDefinition = 0;

    /// Execute the tool with the given parameters.
    /// Returns the tool result as JSON, or an error.
    virtual auto execute(json params) -> awaitable<Result<json>> = 0;
};

} // namespace openclaw::agent
