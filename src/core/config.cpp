#include "openclaw/core/config.hpp"
#include "openclaw/core/logger.hpp"

#include <fstream>

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

} // namespace openclaw
