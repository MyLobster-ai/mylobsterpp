#include "openclaw/agent/dynamic_tool.hpp"

#include <unordered_set>
#include <utility>

namespace openclaw::agent {

namespace {

auto parse_parameters(const nlohmann::json& schema) -> std::vector<ToolParameter> {
    std::vector<ToolParameter> params;
    if (!schema.contains("parameters") || !schema["parameters"].is_object()) {
        return params;
    }
    const auto& root = schema["parameters"];
    const auto* properties = root.contains("properties") && root["properties"].is_object()
        ? &root["properties"] : nullptr;
    if (!properties) {
        return params;
    }
    std::unordered_set<std::string> required_set;
    if (root.contains("required") && root["required"].is_array()) {
        for (const auto& r : root["required"]) {
            if (r.is_string()) {
                required_set.insert(r.get<std::string>());
            }
        }
    }
    for (auto it = properties->begin(); it != properties->end(); ++it) {
        ToolParameter p;
        p.name = it.key();
        const auto& spec = it.value();
        p.type = spec.value("type", "string");
        p.description = spec.value("description", "");
        p.required = required_set.contains(p.name);
        if (spec.contains("default")) {
            p.default_value = spec["default"];
        }
        if (spec.contains("enum") && spec["enum"].is_array()) {
            std::vector<std::string> enum_values;
            for (const auto& e : spec["enum"]) {
                if (e.is_string()) {
                    enum_values.push_back(e.get<std::string>());
                }
            }
            if (!enum_values.empty()) {
                p.enum_values = std::move(enum_values);
            }
        }
        params.push_back(std::move(p));
    }
    return params;
}

} // namespace

auto DynamicTool::from_schema(const nlohmann::json& schema)
    -> Result<std::unique_ptr<DynamicTool>> {
    if (!schema.is_object()) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Tool schema must be a JSON object"));
    }
    auto name = schema.value("name", std::string{});
    if (name.empty()) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Tool schema requires a non-empty 'name'"));
    }
    auto description = schema.value("description", std::string{});
    if (description.empty()) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Tool schema requires a non-empty 'description'",
            name));
    }
    ToolDefinition def;
    def.name = std::move(name);
    def.description = std::move(description);
    def.parameters = parse_parameters(schema);

    nlohmann::json response_template = schema.value("response_template",
                                                    nlohmann::json::object());

    return std::unique_ptr<DynamicTool>(
        new DynamicTool(std::move(def), std::move(response_template)));
}

auto DynamicTool::execute(json params) -> awaitable<Result<json>> {
    if (!response_template_.empty()) {
        // Shallow substitution: clone template, replace {params} sentinel.
        nlohmann::json out = response_template_;
        if (out.is_object() && out.contains("{params}")) {
            out["{params}"] = params;
        }
        co_return out;
    }
    co_return json{
        {"name", def_.name},
        {"params", std::move(params)},
        {"acknowledged", true},
    };
}

} // namespace openclaw::agent
