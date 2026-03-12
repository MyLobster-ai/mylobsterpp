#include "openclaw/core/config.hpp"
#include "openclaw/core/logger.hpp"

#include <fstream>
#include <regex>

namespace openclaw {

auto load_config(const std::filesystem::path& path) -> Config {
    if (!std::filesystem::exists(path)) {
        LOG_WARN("Config file not found: {}, using defaults", path.string());
        return default_config();
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN("Cannot open config file: {}, using defaults", path.string());
        return default_config();
    }

    try {
        json j = json::parse(file);

        // v2026.2.24: Coerce meta.lastTouchedAt from numeric to ISO string
        if (j.contains("meta") && j["meta"].is_object()) {
            if (j["meta"].contains("lastTouchedAt") && j["meta"]["lastTouchedAt"].is_number()) {
                auto ts = j["meta"]["lastTouchedAt"].get<int64_t>();
                // Convert epoch milliseconds to ISO 8601 string
                auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(ts));
                auto time_t_val = std::chrono::system_clock::to_time_t(tp);
                char buf[64];
                struct tm tm_val;
                gmtime_r(&time_t_val, &tm_val);
                strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_val);
                j["meta"]["lastTouchedAt"] = std::string(buf);
                LOG_DEBUG("Config: coerced meta.lastTouchedAt from numeric ({}) to '{}'", ts, buf);
            }
        }

        // v2026.2.24: Treat google-antigravity-auth plugin references as warnings
        if (j.contains("plugins") && j["plugins"].is_array()) {
            auto& plugins = j["plugins"];
            for (auto it = plugins.begin(); it != plugins.end(); ) {
                auto plugin_name = it->value("name", "");
                if (plugin_name == "google-antigravity-auth") {
                    LOG_WARN("Config: ignoring removed plugin '{}' (deprecated in v2026.2.24)", plugin_name);
                    it = plugins.erase(it);
                } else {
                    ++it;
                }
            }
        }

        auto cfg = j.get<Config>();

        // v2026.3.7: Validate gateway auth mode when both token & password exist
        if (!validate_gateway_auth_mode(cfg.gateway)) {
            LOG_ERROR("Config: gateway.auth.mode required when both token and password are set");
            throw std::runtime_error("invalid config: gateway.auth.mode required");
        }

        return cfg;
    } catch (const json::exception& e) {
        // v2026.3.2: Fail-closed on invalid config loads (#9040)
        LOG_ERROR("Failed to parse config: {}", e.what());
        throw std::runtime_error(std::string("Config load failed (fail-closed): ") + e.what());
    }
}

// v2026.3.2: Validate config patch safety constraints.
// Rejects patches that set non-loopback gateway.bind while tailscale serve/funnel is active.
auto validate_config_patch(const json& patch_value, std::string_view patch_path) -> bool {
    if (patch_path == "gateway.bind" || patch_path == "gateway") {
        // Check if tailscale serve or funnel is active
        std::string bind_value;
        if (patch_value.is_string()) {
            bind_value = patch_value.get<std::string>();
        } else if (patch_value.is_object() && patch_value.contains("bind")) {
            bind_value = patch_value.value("bind", "");
        }

        if (bind_value == "all" || bind_value == "0.0.0.0") {
            // Check for active tailscale serve/funnel by looking at env vars
            if (auto* ts = std::getenv("TAILSCALE_SERVE_PORT"); ts && std::string(ts) != "") {
                LOG_WARN("Config patch rejected: cannot set non-loopback gateway.bind "
                         "while tailscale serve is active (TAILSCALE_SERVE_PORT={})", ts);
                return false;
            }
            if (auto* tf = std::getenv("TAILSCALE_FUNNEL"); tf && std::string(tf) == "1") {
                LOG_WARN("Config patch rejected: cannot set non-loopback gateway.bind "
                         "while tailscale funnel is active");
                return false;
            }
        }
    }
    return true;
}

// v2026.3.2: Check if a config update touches model-related paths that
// should trigger heartbeat hot-reload.
auto is_model_config_update(std::string_view path) -> bool {
    return path.starts_with("models.") ||
           path.starts_with("agents.defaults.model") ||
           path == "models" ||
           path == "agents.defaults.model";
}

auto load_config_from_env() -> Config {
    Config config;

    if (auto* val = std::getenv("OPENCLAW_PORT")) {
        config.gateway.port = static_cast<uint16_t>(std::stoi(val));
    }
    if (auto* val = std::getenv("OPENCLAW_BIND")) {
        config.gateway.bind = (std::string(val) == "all") ? BindMode::All : BindMode::Loopback;
    }
    if (auto* val = std::getenv("OPENCLAW_LOG_LEVEL")) {
        config.log_level = val;
    }
    if (auto* val = std::getenv("OPENCLAW_DATA_DIR")) {
        config.data_dir = val;
    }
    if (auto* val = std::getenv("ANTHROPIC_API_KEY")) {
        ProviderConfig anthropic;
        anthropic.name = "anthropic";
        anthropic.api_key = val;
        if (auto* model = std::getenv("ANTHROPIC_MODEL")) {
            anthropic.model = model;
        }
        config.providers.push_back(std::move(anthropic));
    }
    if (auto* val = std::getenv("OPENAI_API_KEY")) {
        ProviderConfig openai;
        openai.name = "openai";
        openai.api_key = val;
        config.providers.push_back(std::move(openai));
    }
    if (auto* val = std::getenv("HUGGINGFACE_API_KEY")) {
        ProviderConfig hf;
        hf.name = "huggingface";
        hf.api_key = val;
        config.providers.push_back(std::move(hf));
    }
    if (auto* val = std::getenv("OLLAMA_BASE_URL")) {
        ProviderConfig ollama;
        ollama.name = "ollama";
        ollama.base_url = val;
        config.providers.push_back(std::move(ollama));
    } else {
        // Auto-detect Ollama if OLLAMA_API_KEY is set (even though it doesn't need one)
        if (std::getenv("OLLAMA_API_KEY")) {
            ProviderConfig ollama;
            ollama.name = "ollama";
            ollama.api_key = std::getenv("OLLAMA_API_KEY");
            config.providers.push_back(std::move(ollama));
        }
    }
    if (auto* val = std::getenv("SYNTHETIC_API_KEY")) {
        ProviderConfig synthetic;
        synthetic.name = "synthetic";
        synthetic.api_key = val;
        config.providers.push_back(std::move(synthetic));
    }
    if (auto* val = std::getenv("MISTRAL_API_KEY")) {
        ProviderConfig mistral;
        mistral.name = "mistral";
        mistral.api_key = val;
        config.providers.push_back(std::move(mistral));
    }
    if (auto* val = std::getenv("VOLCENGINE_API_KEY")) {
        ProviderConfig volcengine;
        volcengine.name = "volcengine";
        volcengine.api_key = val;
        config.providers.push_back(std::move(volcengine));
    }
    // v2026.3.7: Alibaba Cloud Model Studio (renamed from Bailian)
    if (auto* val = std::getenv("MODELSTUDIO_API_KEY")) {
        ProviderConfig modelstudio;
        modelstudio.name = "modelstudio";
        modelstudio.api_key = val;
        config.providers.push_back(std::move(modelstudio));
    }
    // v2026.3.11: Gemini API key for embeddings
    if (auto* val = std::getenv("GEMINI_API_KEY")) {
        ProviderConfig gemini;
        gemini.name = "gemini";
        gemini.api_key = val;
        config.providers.push_back(std::move(gemini));
    }

    return config;
}

auto default_config() -> Config {
    return Config{};
}

auto default_data_dir() -> std::filesystem::path {
    if (auto* val = std::getenv("OPENCLAW_DATA_DIR")) {
        return val;
    }
    auto home = std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "/tmp");
    return home / ".openclaw";
}

auto resolve_env_refs(std::string_view input) -> std::string {
    std::string result;
    result.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        // Check for $$ escape
        if (i + 1 < input.size() && input[i] == '$' && input[i + 1] == '$') {
            // Escaped: $${VAR} -> literal ${VAR}
            result += '$';
            i += 2;
            continue;
        }

        // Check for ${VAR} pattern
        if (i + 2 < input.size() && input[i] == '$' && input[i + 1] == '{') {
            auto close = input.find('}', i + 2);
            if (close != std::string_view::npos) {
                auto var_name = input.substr(i + 2, close - i - 2);
                std::string var_name_str(var_name);

                if (auto* val = std::getenv(var_name_str.c_str())) {
                    result += val;
                } else {
                    // Preserve unresolved refs
                    result += input.substr(i, close - i + 1);
                    LOG_DEBUG("Config: unresolved env ref ${{{}}}", var_name);
                }
                i = close + 1;
                continue;
            }
        }

        result += input[i];
        ++i;
    }

    return result;
}

// ---------------------------------------------------------------------------
// v2026.3.2: Tilde path expansion
// ---------------------------------------------------------------------------

auto expand_tilde(std::string_view path) -> std::string {
    if (path.empty() || path[0] != '~') {
        return std::string(path);
    }

    // ~/... → $HOME/...
    if (path.size() == 1 || path[1] == '/') {
        const char* home = std::getenv("HOME");
        if (!home) {
#ifdef _WIN32
            home = std::getenv("USERPROFILE");
#endif
        }
        if (home) {
            std::string result(home);
            if (path.size() > 1) {
                result += path.substr(1);
            }
            return result;
        }
    }

    return std::string(path);
}

// ---------------------------------------------------------------------------
// v2026.3.7: Gateway auth mode validation
// ---------------------------------------------------------------------------

auto validate_gateway_auth_mode(const GatewayConfig& config) -> bool {
    if (!config.auth) return true;
    const auto& auth = *config.auth;

    bool has_token = auth.token.has_value() && !auth.token->empty();
    bool has_password = auth.tailscale_authkey.has_value() && !auth.tailscale_authkey->empty();

    // If both credentials exist, auth_mode must be explicitly set
    if (has_token && has_password) {
        return config.auth_mode.has_value() && !config.auth_mode->empty();
    }
    return true;
}

// ---------------------------------------------------------------------------
// v2026.3.8: Brave search language code validation
// ---------------------------------------------------------------------------

auto validate_brave_language_code(std::string_view code) -> bool {
    static const std::vector<std::string_view> valid_codes = {
        "ar", "bg", "bn", "ca", "cs", "da", "de", "el", "en", "es",
        "et", "fa", "fi", "fr", "gu", "he", "hi", "hr", "hu", "id",
        "it", "ja", "kn", "ko", "lt", "lv", "ml", "mr", "ms", "nb",
        "nl", "pa", "pl", "pt", "ro", "ru", "sk", "sl", "sr", "sv",
        "sw", "ta", "te", "th", "tr", "uk", "ur", "vi", "zh",
        // Extended codes
        "zh-hans", "zh-hant", "en-gb", "pt-br",
    };
    for (auto v : valid_codes) {
        if (v == code) return true;
    }
    return false;
}

} // namespace openclaw
