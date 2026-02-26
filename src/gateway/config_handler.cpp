#include "openclaw/gateway/config_handler.hpp"

#include <fstream>

#include <boost/asio/use_awaitable.hpp>
#include <openssl/sha.h>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

// ---------------------------------------------------------------------------
// RuntimeConfig
// ---------------------------------------------------------------------------

RuntimeConfig::RuntimeConfig(const Config& initial_config)
    : config_(initial_config)
    , default_config_(initial_config) {}

auto RuntimeConfig::get(std::string_view path) const -> json {
    std::lock_guard lock(mutex_);
    auto* node = navigate(config_, path);
    if (!node) return json(nullptr);
    return *node;
}

void RuntimeConfig::set(std::string_view path, const json& value) {
    std::lock_guard lock(mutex_);
    auto* node = navigate(config_, path, /*create=*/true);
    if (node) {
        *node = value;
        persist();
    }
}

auto RuntimeConfig::patch(
    const std::vector<std::pair<std::string, json>>& patches,
    const std::string& base_hash) -> bool {
    std::lock_guard lock(mutex_);

    // Optimistic concurrency check.
    if (!base_hash.empty() && compute_hash(config_) != base_hash) {
        return false;
    }

    for (const auto& [path, value] : patches) {
        auto* node = navigate(config_, path, /*create=*/true);
        if (node) {
            *node = value;
        }
    }

    persist();
    return true;
}

auto RuntimeConfig::hash() const -> std::string {
    std::lock_guard lock(mutex_);
    return compute_hash(config_);
}

auto RuntimeConfig::to_json() const -> json {
    std::lock_guard lock(mutex_);
    return config_;
}

void RuntimeConfig::reset() {
    std::lock_guard lock(mutex_);
    config_ = default_config_;
    persist();
}

void RuntimeConfig::set_persist_path(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);
    persist_path_ = path;
}

auto RuntimeConfig::list_keys() const -> std::vector<std::string> {
    std::lock_guard lock(mutex_);
    std::vector<std::string> keys;
    if (config_.is_object()) {
        for (auto it = config_.begin(); it != config_.end(); ++it) {
            keys.push_back(it.key());
        }
    }
    return keys;
}

void RuntimeConfig::persist() const {
    if (persist_path_.empty()) return;
    try {
        std::ofstream ofs(persist_path_);
        if (ofs.is_open()) {
            ofs << config_.dump(2);
        }
    } catch (const std::exception& e) {
        LOG_WARN("Failed to persist config: {}", e.what());
    }
}

auto RuntimeConfig::navigate(json& root, std::string_view path, bool create)
    -> json* {
    json* current = &root;
    size_t pos = 0;

    while (pos < path.size()) {
        auto dot = path.find('.', pos);
        auto segment = std::string(path.substr(pos, dot - pos));
        pos = (dot == std::string_view::npos) ? path.size() : dot + 1;

        if (!current->is_object()) {
            if (create) {
                *current = json::object();
            } else {
                return nullptr;
            }
        }

        if (!current->contains(segment)) {
            if (create) {
                (*current)[segment] = json::object();
            } else {
                return nullptr;
            }
        }

        current = &(*current)[segment];
    }

    return current;
}

auto RuntimeConfig::navigate(const json& root, std::string_view path)
    -> const json* {
    const json* current = &root;
    size_t pos = 0;

    while (pos < path.size()) {
        auto dot = path.find('.', pos);
        auto segment = std::string(path.substr(pos, dot - pos));
        pos = (dot == std::string_view::npos) ? path.size() : dot + 1;

        if (!current->is_object() || !current->contains(segment)) {
            return nullptr;
        }

        current = &(*current).at(segment);
    }

    return current;
}

auto RuntimeConfig::compute_hash(const json& j) -> std::string {
    auto serialized = j.dump();
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(serialized.data()),
           serialized.size(), hash);

    std::string hex;
    hex.reserve(SHA256_DIGEST_LENGTH * 2);
    for (unsigned char byte : hash) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", byte);
        hex += buf;
    }
    return hex;
}

// ---------------------------------------------------------------------------
// Handler registration
// ---------------------------------------------------------------------------

void register_config_handlers(Protocol& protocol,
                              RuntimeConfig& runtime_config) {
    // config.get — bridge calls from config_client.rs:79
    protocol.register_method("config.get",
        [&runtime_config](json params) -> awaitable<json> {
            auto path = params.value("path", "");
            if (path.empty()) {
                co_return json{{"ok", false}, {"error", "path is required"}};
            }
            auto value = runtime_config.get(path);
            auto hash = runtime_config.hash();
            co_return json{{"value", value}, {"hash", hash}};
        },
        "Get configuration value by key", "config");

    // config.set
    protocol.register_method("config.set",
        [&runtime_config](json params) -> awaitable<json> {
            auto path = params.value("path", "");
            if (path.empty()) {
                co_return json{{"ok", false}, {"error", "path is required"}};
            }
            auto value = params.value("value", json(nullptr));
            runtime_config.set(path, value);
            co_return json{{"ok", true}};
        },
        "Set configuration value", "config");

    // config.patch — bridge calls from config_client.rs:96
    protocol.register_method("config.patch",
        [&runtime_config](json params) -> awaitable<json> {
            auto base_hash = params.value("baseHash", "");
            auto patches_json = params.value("patches", json::array());

            std::vector<std::pair<std::string, json>> patches;
            for (const auto& p : patches_json) {
                patches.emplace_back(
                    p.value("path", ""),
                    p.value("value", json(nullptr)));
            }

            bool ok = runtime_config.patch(patches, base_hash);
            if (!ok) {
                co_return json{
                    {"ok", false},
                    {"error", "Config has been modified since baseHash was computed"},
                };
            }
            co_return json{{"ok", true}};
        },
        "Apply config patches with optimistic concurrency", "config");

    // config.list
    protocol.register_method("config.list",
        [&runtime_config](json /*params*/) -> awaitable<json> {
            auto keys = runtime_config.list_keys();
            co_return json{{"keys", keys}};
        },
        "List all configuration keys", "config");

    // config.reset
    protocol.register_method("config.reset",
        [&runtime_config](json /*params*/) -> awaitable<json> {
            runtime_config.reset();
            co_return json{{"ok", true}};
        },
        "Reset configuration to defaults", "config");

    // config.export
    protocol.register_method("config.export",
        [&runtime_config](json /*params*/) -> awaitable<json> {
            co_return runtime_config.to_json();
        },
        "Export full configuration as JSON", "config");

    // config.import
    protocol.register_method("config.import",
        [&runtime_config](json params) -> awaitable<json> {
            auto config_data = params.value("config", json::object());
            if (!config_data.is_object()) {
                co_return json{{"ok", false}, {"error", "config must be a JSON object"}};
            }
            // Apply all top-level keys from the imported config.
            for (auto it = config_data.begin(); it != config_data.end(); ++it) {
                runtime_config.set(it.key(), it.value());
            }
            co_return json{{"ok", true}};
        },
        "Import configuration from JSON", "config");

    LOG_INFO("Registered config handlers: config.get, config.set, config.patch, "
             "config.list, config.reset, config.export, config.import");
}

} // namespace openclaw::gateway
