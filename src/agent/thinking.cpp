#include "openclaw/agent/thinking.hpp"

namespace openclaw::agent {

void apply_thinking_anthropic(json& request_body, const ThinkingConfig& config) {
    if (config.mode == ThinkingMode::None) return;

    // Anthropic extended thinking uses the "thinking" parameter
    // See: https://docs.anthropic.com/en/docs/build-with-claude/extended-thinking
    json thinking;
    thinking["type"] = "enabled";

    if (config.mode == ThinkingMode::Extended && config.budget_tokens.has_value()) {
        thinking["budget_tokens"] = *config.budget_tokens;
    } else if (config.mode == ThinkingMode::Extended) {
        // Default budget for extended thinking
        thinking["budget_tokens"] = 10000;
    } else {
        // Basic thinking -- smaller budget
        thinking["budget_tokens"] = 5000;
    }

    request_body["thinking"] = thinking;

    // When thinking is enabled, temperature must not be set (Anthropic constraint)
    // or must be set to 1.0
    if (request_body.contains("temperature")) {
        request_body.erase("temperature");
    }
}

void apply_thinking_openai(json& request_body, const ThinkingConfig& config) {
    if (config.mode == ThinkingMode::None) return;

    // OpenAI uses "reasoning_effort" for o1/o3 models
    if (config.mode == ThinkingMode::Extended) {
        request_body["reasoning_effort"] = "high";
    } else {
        request_body["reasoning_effort"] = "medium";
    }

    // When reasoning is enabled, temperature and top_p should not be set
    if (request_body.contains("temperature")) {
        request_body.erase("temperature");
    }
    if (request_body.contains("top_p")) {
        request_body.erase("top_p");
    }
}

auto thinking_config_from_mode(ThinkingMode mode) -> ThinkingConfig {
    ThinkingConfig config;
    config.mode = mode;

    switch (mode) {
        case ThinkingMode::None:
            break;
        case ThinkingMode::Basic:
            config.budget_tokens = 5000;
            config.include_in_response = false;
            break;
        case ThinkingMode::Extended:
            config.budget_tokens = 10000;
            config.include_in_response = true;
            break;
    }

    return config;
}

auto parse_thinking_mode(std::string_view mode_str) -> ThinkingMode {
    if (mode_str == "basic") return ThinkingMode::Basic;
    if (mode_str == "extended") return ThinkingMode::Extended;
    return ThinkingMode::None;
}

} // namespace openclaw::agent
