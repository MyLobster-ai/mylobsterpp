#pragma once

#include <memory>

#include <nlohmann/json.hpp>

#include "openclaw/agent/tool.hpp"

namespace openclaw::agent {

/// Tool whose definition (name, description, parameter schema) is supplied
/// at runtime as JSON, and whose execution returns a fixed acknowledgement
/// payload echoing the inputs.
///
/// This is the building block behind the gateway's `tool.register` RPC: a
/// caller hands the gateway a JSON Schema-shaped tool spec, and the gateway
/// stores a DynamicTool that subsequent `tool.execute` calls can invoke.
/// The default execution behaviour returns
///     { "name": <tool>, "params": <input>, "acknowledged": true }
/// so callers can wire upstream logic (LLM tool-loop, audit trail) without
/// the host needing to embed a scripting runtime. A response_template can be
/// supplied to override the payload — its `{params}` placeholder is replaced
/// with the call's params.
class DynamicTool final : public Tool {
public:
    /// Construct a DynamicTool from a JSON schema-style description.
    /// Required fields: `name`, `description`. Optional: `parameters` (JSON
    /// Schema object), `response_template` (json — returned on execute()).
    static auto from_schema(const nlohmann::json& schema)
        -> Result<std::unique_ptr<DynamicTool>>;

    [[nodiscard]] auto definition() const -> ToolDefinition override {
        return def_;
    }

    auto execute(json params) -> awaitable<Result<json>> override;

private:
    DynamicTool(ToolDefinition def, nlohmann::json response_template)
        : def_(std::move(def)),
          response_template_(std::move(response_template)) {}

    ToolDefinition def_;
    nlohmann::json response_template_;
};

} // namespace openclaw::agent
