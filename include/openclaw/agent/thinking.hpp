#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "openclaw/core/types.hpp"

namespace openclaw::agent {

using json = nlohmann::json;

/// Configuration for thinking / chain-of-thought behavior.
///
/// Thinking modes control how the AI model reasons about its response:
///
/// - None: Standard completion, no thinking output.
/// - Basic: The model includes its reasoning in the response. Supported
///   by Anthropic Claude (thinking blocks) and OpenAI (reasoning tokens).
/// - Extended: Extended thinking with a configurable budget of tokens.
///   The model spends more time reasoning before producing a final answer.
struct ThinkingConfig {
    ThinkingMode mode = ThinkingMode::None;

    /// Maximum number of tokens allocated for thinking (Extended mode only).
    /// If not set, the provider uses its default budget.
    std::optional<int> budget_tokens;

    /// Whether to include thinking content in the response message.
    /// If false, thinking is used internally but not returned to the caller.
    bool include_in_response = false;
};

/// Apply thinking configuration to an Anthropic API request body.
/// Modifies the JSON request in-place to include thinking parameters.
void apply_thinking_anthropic(json& request_body, const ThinkingConfig& config);

/// Apply thinking configuration to an OpenAI API request body.
/// Modifies the JSON request in-place to include reasoning parameters.
void apply_thinking_openai(json& request_body, const ThinkingConfig& config);

/// Determine the ThinkingConfig from a ThinkingMode enum value.
/// Uses sensible defaults for budget_tokens and include_in_response.
auto thinking_config_from_mode(ThinkingMode mode) -> ThinkingConfig;

/// Convert a string representation to a ThinkingMode.
/// Valid values: "none", "basic", "extended". Returns None for unknown.
auto parse_thinking_mode(std::string_view mode_str) -> ThinkingMode;

} // namespace openclaw::agent
