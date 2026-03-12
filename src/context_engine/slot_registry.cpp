#include "openclaw/context_engine/slot_registry.hpp"
#include "openclaw/core/logger.hpp"

namespace openclaw::context_engine {

SlotRegistry::SlotRegistry()
    : active_(std::make_shared<LegacyContextEngine>()) {}

SlotRegistry::~SlotRegistry() = default;

void SlotRegistry::resolve(const ContextEngineSlotConfig& config) {
    std::lock_guard lock(mutex_);

    if (!config.plugin_id) {
        active_ = std::make_shared<LegacyContextEngine>();
        LOG_INFO("Context engine: using legacy (no plugin configured)");
        return;
    }

    auto it = engines_.find(*config.plugin_id);
    if (it != engines_.end()) {
        active_ = it->second;
        LOG_INFO("Context engine: resolved to plugin '{}'", *config.plugin_id);
    } else {
        LOG_WARN("Context engine: plugin '{}' not found, falling back to legacy",
                 *config.plugin_id);
        active_ = std::make_shared<LegacyContextEngine>();
    }
}

auto SlotRegistry::active() const -> std::shared_ptr<ContextEngine> {
    std::lock_guard lock(mutex_);
    return active_;
}

void SlotRegistry::register_engine(std::shared_ptr<ContextEngine> engine) {
    std::lock_guard lock(mutex_);
    auto id = std::string(engine->id());
    LOG_INFO("Context engine: registered '{}'", id);
    engines_[id] = std::move(engine);
}

auto SlotRegistry::has_engine(std::string_view id) const -> bool {
    std::lock_guard lock(mutex_);
    return engines_.contains(std::string(id));
}

void SlotRegistry::reset() {
    std::lock_guard lock(mutex_);
    active_ = std::make_shared<LegacyContextEngine>();
    LOG_INFO("Context engine: reset to legacy");
}

} // namespace openclaw::context_engine
