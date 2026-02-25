#include "openclaw/infra/security_audit.hpp"

namespace openclaw::infra {

auto collect_multi_user_findings(const Config& config) -> std::vector<SecurityFinding> {
    std::vector<SecurityFinding> findings;

    // Check for channels that accept DMs from anyone
    for (const auto& ch : config.channels) {
        if (!ch.enabled) continue;

        if (ch.type == "telegram") {
            auto dm_policy = ch.settings.value("dm_policy", "open");
            if (dm_policy == "open") {
                findings.push_back({
                    .category = "multi_user",
                    .severity = "warning",
                    .message = "Telegram channel '" + ch.settings.value("channel_name", "telegram") +
                               "' has dm_policy='open' - any Telegram user can send DMs",
                    .remediation = "Set dm_policy to 'allowlist' and configure allowed_sender_ids",
                });
            }
        }

        if (ch.type == "discord" || ch.type == "slack") {
            findings.push_back({
                .category = "multi_user",
                .severity = "info",
                .message = ch.type + " channel '" + ch.settings.value("channel_name", ch.type) +
                           "' is enabled - group channels are inherently multi-user",
                .remediation = "Ensure appropriate tool exposure settings for multi-user context",
            });
        }
    }

    // Check sandbox vs tool exposure
    if (!config.sandbox.enabled && config.browser.enabled) {
        findings.push_back({
            .category = "tool_exposure",
            .severity = "warning",
            .message = "Browser tools enabled without sandbox isolation",
            .remediation = "Enable sandbox mode when browser tools are active",
        });
    }

    return findings;
}

auto list_potential_multi_user_signals(const Config& config) -> std::vector<std::string> {
    std::vector<std::string> signals;

    int enabled_channels = 0;
    for (const auto& ch : config.channels) {
        if (ch.enabled) enabled_channels++;
    }

    if (enabled_channels > 1) {
        signals.push_back("Multiple channels enabled (" + std::to_string(enabled_channels) + ")");
    }

    for (const auto& ch : config.channels) {
        if (!ch.enabled) continue;
        if (ch.type == "discord" || ch.type == "slack") {
            signals.push_back(ch.type + " channel active (group messaging)");
        }
    }

    if (config.heartbeat.target != "none") {
        signals.push_back("Heartbeat target is '" + config.heartbeat.target + "' (outbound messaging)");
    }

    return signals;
}

auto collect_risky_tool_exposure(const Config& config) -> std::vector<SecurityFinding> {
    std::vector<SecurityFinding> findings;

    if (!config.sandbox.enabled) {
        if (config.browser.enabled) {
            findings.push_back({
                .category = "tool_exposure",
                .severity = "warning",
                .message = "Browser tool enabled without sandbox - browsed pages have unrestricted filesystem access",
                .remediation = "Enable sandbox.enabled=true or disable browser tools",
            });
        }
    }

    return findings;
}

} // namespace openclaw::infra
