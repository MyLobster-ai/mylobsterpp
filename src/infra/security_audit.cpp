#include "openclaw/infra/security_audit.hpp"
#include "openclaw/core/logger.hpp"

#include <filesystem>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

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

// ---------------------------------------------------------------------------
// v2026.3.2: Additional security audit functions
// ---------------------------------------------------------------------------

auto validate_bootstrap_seed_boundary(
    const std::filesystem::path& seed_path,
    const std::filesystem::path& workspace_root) -> bool
{
    namespace fs = std::filesystem;
    std::error_code ec;

    // Reject symlinks
    if (fs::is_symlink(seed_path, ec) && !ec) {
        LOG_WARN("Bootstrap seed file is a symlink: {}", seed_path.string());
        return false;
    }

    // Reject hardlinks (nlink > 1)
    if (fs::is_regular_file(seed_path, ec) && !ec) {
        auto nlink = fs::hard_link_count(seed_path, ec);
        if (!ec && nlink > 1) {
            LOG_WARN("Bootstrap seed file has {} hard links: {}", nlink, seed_path.string());
            return false;
        }
    }

    // Canonical containment check
    auto canonical_seed = fs::canonical(seed_path, ec);
    if (ec) {
        LOG_WARN("Cannot canonicalize bootstrap seed: {} ({})", seed_path.string(), ec.message());
        return false;
    }

    auto canonical_root = fs::canonical(workspace_root, ec);
    if (ec) {
        LOG_WARN("Cannot canonicalize workspace root: {} ({})", workspace_root.string(), ec.message());
        return false;
    }

    auto root_str = canonical_root.string();
    if (!root_str.ends_with('/')) root_str += '/';

    if (!canonical_seed.string().starts_with(root_str) &&
        canonical_seed != canonical_root) {
        LOG_WARN("Bootstrap seed escapes workspace: {} not in {}",
                 canonical_seed.string(), canonical_root.string());
        return false;
    }

    return true;
}

auto is_safe_regex_pattern(std::string_view pattern) -> bool {
    // Detect patterns with quantified ambiguous alternation that could
    // cause catastrophic backtracking: (a+|b+)+ or (a|b)*+ etc.
    int paren_depth = 0;
    bool has_alternation = false;
    bool has_quantifier_after_group = false;

    for (size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];

        // Skip escaped characters
        if (c == '\\' && i + 1 < pattern.size()) {
            ++i;
            continue;
        }

        if (c == '(') {
            paren_depth++;
            has_alternation = false;
        } else if (c == ')') {
            paren_depth--;
            // Check if the group is followed by a quantifier
            if (i + 1 < pattern.size()) {
                char next = pattern[i + 1];
                if (next == '+' || next == '*' || next == '{') {
                    has_quantifier_after_group = true;
                }
            }
            if (has_alternation && has_quantifier_after_group) {
                // Check if alternation branches have their own quantifiers
                // This is the dangerous pattern: (a+|b+)+
                return false;
            }
        } else if (c == '|' && paren_depth > 0) {
            has_alternation = true;
        }
    }

    return true;
}

auto bound_regex_input(std::string_view input, size_t max_bytes)
    -> std::string_view
{
    if (input.size() <= max_bytes) return input;
    return input.substr(0, max_bytes);
}

auto strip_inbound_metadata_sentinels(std::string_view content) -> std::string {
    std::string result;
    result.reserve(content.size());

    // Strip known metadata sentinels: [METADATA], {METADATA}, <METADATA>
    // and their variations with different brackets
    size_t i = 0;
    while (i < content.size()) {
        // Check for sentinel patterns
        bool is_sentinel = false;

        if (i + 10 < content.size()) {
            auto candidate = content.substr(i, 10);
            if (candidate == "[METADATA]" || candidate == "{METADATA}" ||
                candidate == "<METADATA>") {
                is_sentinel = true;
                i += 10;
            }
        }

        // Also check for JSON fence patterns that might contain injection
        if (!is_sentinel && i + 3 < content.size() &&
            content[i] == '`' && content[i + 1] == '`' && content[i + 2] == '`') {
            // Skip code fences that start with ```json{metadata
            auto fence_end = content.find("```", i + 3);
            if (fence_end != std::string_view::npos) {
                auto fence_content = content.substr(i + 3, fence_end - i - 3);
                if (fence_content.find("METADATA") != std::string_view::npos ||
                    fence_content.find("metadata") != std::string_view::npos) {
                    i = fence_end + 3;
                    is_sentinel = true;
                }
            }
        }

        if (!is_sentinel) {
            result += content[i];
            ++i;
        }
    }

    return result;
}

auto normalize_external_content_markers(std::string_view content) -> std::string {
    std::string result;
    result.reserve(content.size());

    size_t i = 0;
    while (i < content.size()) {
        auto c = static_cast<unsigned char>(content[i]);

        // Handle UTF-8 multi-byte sequences for angle bracket homoglyphs
        if (c >= 0xC0 && i + 1 < content.size()) {
            auto c2 = static_cast<unsigned char>(content[i + 1]);

            // 2-byte UTF-8 sequences
            if (c == 0xC2 && c2 == 0xAB) {
                result += '<';  // « LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
                i += 2;
                continue;
            }
            if (c == 0xC2 && c2 == 0xBB) {
                result += '>';  // » RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
                i += 2;
                continue;
            }

            // 3-byte UTF-8 sequences
            if (c >= 0xE0 && i + 2 < content.size()) {
                auto c3 = static_cast<unsigned char>(content[i + 2]);

                // ‹ U+2039 SINGLE LEFT-POINTING ANGLE QUOTATION MARK
                if (c == 0xE2 && c2 == 0x80 && c3 == 0xB9) {
                    result += '<';
                    i += 3;
                    continue;
                }
                // › U+203A SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
                if (c == 0xE2 && c2 == 0x80 && c3 == 0xBA) {
                    result += '>';
                    i += 3;
                    continue;
                }
                // ⟨ U+27E8 MATHEMATICAL LEFT ANGLE BRACKET
                if (c == 0xE2 && c2 == 0x9F && c3 == 0xA8) {
                    result += '<';
                    i += 3;
                    continue;
                }
                // ⟩ U+27E9 MATHEMATICAL RIGHT ANGLE BRACKET
                if (c == 0xE2 && c2 == 0x9F && c3 == 0xA9) {
                    result += '>';
                    i += 3;
                    continue;
                }
                // ﹤ U+FE64 SMALL LESS-THAN SIGN
                if (c == 0xEF && c2 == 0xB9 && c3 == 0xA4) {
                    result += '<';
                    i += 3;
                    continue;
                }
                // ﹥ U+FE65 SMALL GREATER-THAN SIGN
                if (c == 0xEF && c2 == 0xB9 && c3 == 0xA5) {
                    result += '>';
                    i += 3;
                    continue;
                }
                // ＜ U+FF1C FULLWIDTH LESS-THAN SIGN
                if (c == 0xEF && c2 == 0xBC && c3 == 0x9C) {
                    result += '<';
                    i += 3;
                    continue;
                }
                // ＞ U+FF1E FULLWIDTH GREATER-THAN SIGN
                if (c == 0xEF && c2 == 0xBC && c3 == 0x9E) {
                    result += '>';
                    i += 3;
                    continue;
                }
            }
        }

        result += content[i];
        ++i;
    }

    return result;
}

auto validate_skill_metadata(const nlohmann::json& metadata) -> bool {
    // Reject metadata with suspicious keys that could indicate injection
    static const std::vector<std::string> dangerous_keys = {
        "__proto__", "constructor", "prototype",
        "toString", "valueOf", "hasOwnProperty",
    };

    if (!metadata.is_object()) return false;

    for (auto it = metadata.begin(); it != metadata.end(); ++it) {
        auto key = it.key();

        // Check for dangerous keys
        for (const auto& dk : dangerous_keys) {
            if (key == dk) {
                LOG_WARN("Skill metadata contains dangerous key: {}", key);
                return false;
            }
        }

        // Keys should be reasonable length
        if (key.size() > 256) {
            LOG_WARN("Skill metadata key too long: {} chars", key.size());
            return false;
        }

        // String values should be bounded
        if (it.value().is_string() && it.value().get<std::string>().size() > 65536) {
            LOG_WARN("Skill metadata value too large for key: {}", key);
            return false;
        }
    }

    return true;
}

auto normalize_platform_string(std::string_view platform) -> std::string {
    std::string result;
    result.reserve(platform.size());

    for (size_t i = 0; i < platform.size(); ++i) {
        auto c = static_cast<unsigned char>(platform[i]);

        // Only allow ASCII alphanumeric, hyphen, underscore, period
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            result += static_cast<char>(c);
        }
        // Skip multi-byte UTF-8 sequences (Unicode confusables)
        else if (c >= 0xC0) {
            // Skip the continuation bytes
            if (c >= 0xF0 && i + 3 < platform.size()) i += 3;
            else if (c >= 0xE0 && i + 2 < platform.size()) i += 2;
            else if (c >= 0xC0 && i + 1 < platform.size()) i += 1;
        }
        // Skip other non-ASCII/control characters
    }

    return result;
}

auto harden_backup_permissions(const std::filesystem::path& backup_path) -> bool {
#ifndef _WIN32
    if (::chmod(backup_path.c_str(), 0600) != 0) {
        LOG_WARN("Failed to set 0600 permissions on backup: {} ({})",
                 backup_path.string(), strerror(errno));
        return false;
    }
    return true;
#else
    (void)backup_path;
    return true;  // Windows doesn't support Unix permissions
#endif
}

auto sanitize_log_command(std::string_view command) -> std::string {
    std::string result;
    result.reserve(command.size());

    // Remove any backtick-based command substitution and eval patterns
    bool in_backtick = false;
    for (size_t i = 0; i < command.size(); ++i) {
        char c = command[i];
        if (c == '`') {
            in_backtick = !in_backtick;
            result += '\'';  // Replace backtick with single quote
            continue;
        }
        if (in_backtick) {
            result += '*';  // Mask content inside backticks
            continue;
        }

        // Replace $(...) command substitution
        if (c == '$' && i + 1 < command.size() && command[i + 1] == '(') {
            auto close = command.find(')', i + 2);
            if (close != std::string_view::npos) {
                result += "$(...)";
                i = close;
                continue;
            }
        }

        result += c;
    }

    return result;
}

} // namespace openclaw::infra
