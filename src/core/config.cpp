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

        return j.get<Config>();
    } catch (const json::exception& e) {
        LOG_ERROR("Failed to parse config: {}", e.what());
        return default_config();
    }
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

} // namespace openclaw
