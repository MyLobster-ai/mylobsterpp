#include "openclaw/agent/tool.hpp"

namespace openclaw::agent {

auto ToolDefinition::to_json() const -> json {
    // Produce a generic JSON Schema representation
    json j;
    j["name"] = name;
    j["description"] = description;

    json schema;
    schema["type"] = "object";

    json properties;
    json required_params = json::array();

    for (const auto& param : parameters) {
        json prop;
        prop["type"] = param.type;
        prop["description"] = param.description;

        if (param.default_value.has_value()) {
            prop["default"] = *param.default_value;
        }
        if (param.enum_values.has_value()) {
            prop["enum"] = *param.enum_values;
        }

        properties[param.name] = prop;

        if (param.required) {
            required_params.push_back(param.name);
        }
    }

    schema["properties"] = properties;
    if (!required_params.empty()) {
        schema["required"] = required_params;
    }

    j["input_schema"] = schema;
    return j;
}

auto ToolDefinition::to_anthropic_json() const -> json {
    // Anthropic tool format:
    // {
    //   "name": "...",
    //   "description": "...",
    //   "input_schema": { "type": "object", "properties": {...}, "required": [...] }
    // }
    return to_json();  // Our default format matches Anthropic
}

auto ToolDefinition::to_openai_json() const -> json {
    // OpenAI function tool format:
    // {
    //   "type": "function",
    //   "function": {
    //     "name": "...",
    //     "description": "...",
    //     "parameters": { "type": "object", "properties": {...}, "required": [...] }
    //   }
    // }
    json j;
    j["type"] = "function";

    json func;
    func["name"] = name;
    func["description"] = description;

    json schema;
    schema["type"] = "object";

    json properties;
    json required_params = json::array();

    for (const auto& param : parameters) {
        json prop;
        prop["type"] = param.type;
        prop["description"] = param.description;

        if (param.default_value.has_value()) {
            prop["default"] = *param.default_value;
        }
        if (param.enum_values.has_value()) {
            prop["enum"] = *param.enum_values;
        }

        properties[param.name] = prop;

        if (param.required) {
            required_params.push_back(param.name);
        }
    }

    schema["properties"] = properties;
    if (!required_params.empty()) {
        schema["required"] = required_params;
    }

    func["parameters"] = schema;
    j["function"] = func;

    return j;
}

} // namespace openclaw::agent
