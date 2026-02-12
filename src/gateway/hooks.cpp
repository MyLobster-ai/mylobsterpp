#include "openclaw/gateway/hooks.hpp"

#include "openclaw/core/logger.hpp"

#include <algorithm>

#include <boost/asio/use_awaitable.hpp>

namespace openclaw::gateway {

// -- Insertion helper: keep sorted by priority (ascending numeric value) --

void HookRegistry::insert_sorted(HookList& list, HookEntry entry) {
    auto it = std::ranges::lower_bound(list, entry, [](const HookEntry& a, const HookEntry& b) {
        return static_cast<int>(a.priority) < static_cast<int>(b.priority);
    });
    list.insert(it, std::move(entry));
}

// -- Registration --

void HookRegistry::before(std::string_view method, std::string name, Hook hook,
                          HookPriority priority) {
    LOG_DEBUG("Registering before hook '{}' for method '{}'", name, method);
    insert_sorted(before_hooks_[std::string(method)],
                  HookEntry{std::move(name), std::move(hook), priority});
}

void HookRegistry::after(std::string_view method, std::string name, Hook hook,
                         HookPriority priority) {
    LOG_DEBUG("Registering after hook '{}' for method '{}'", name, method);
    insert_sorted(after_hooks_[std::string(method)],
                  HookEntry{std::move(name), std::move(hook), priority});
}

void HookRegistry::before_all(std::string name, Hook hook,
                               HookPriority priority) {
    before("*", std::move(name), std::move(hook), priority);
}

void HookRegistry::after_all(std::string name, Hook hook,
                              HookPriority priority) {
    after("*", std::move(name), std::move(hook), priority);
}

// -- Removal --

auto HookRegistry::remove_before(std::string_view method, std::string_view name)
    -> bool {
    auto it = before_hooks_.find(std::string(method));
    if (it == before_hooks_.end()) return false;

    auto& list = it->second;
    auto erased = std::erase_if(list, [&](const HookEntry& e) {
        return e.name == name;
    });
    return erased > 0;
}

auto HookRegistry::remove_after(std::string_view method, std::string_view name)
    -> bool {
    auto it = after_hooks_.find(std::string(method));
    if (it == after_hooks_.end()) return false;

    auto& list = it->second;
    auto erased = std::erase_if(list, [&](const HookEntry& e) {
        return e.name == name;
    });
    return erased > 0;
}

// -- Collecting hooks: merge wildcard + method-specific, sorted by priority --

auto HookRegistry::collect_hooks(
    const std::unordered_map<std::string, HookList>& map,
    std::string_view method) -> HookList {
    HookList merged;

    // Wildcard hooks.
    if (auto it = map.find("*"); it != map.end()) {
        merged.insert(merged.end(), it->second.begin(), it->second.end());
    }

    // Method-specific hooks.
    if (auto it = map.find(std::string(method)); it != map.end()) {
        merged.insert(merged.end(), it->second.begin(), it->second.end());
    }

    // Sort the merged list by priority.
    std::ranges::sort(merged, [](const HookEntry& a, const HookEntry& b) {
        return static_cast<int>(a.priority) < static_cast<int>(b.priority);
    });

    return merged;
}

// -- Running a hook chain --

auto HookRegistry::run_chain(const HookList& hooks, json ctx) -> awaitable<json> {
    json current = std::move(ctx);
    for (const auto& entry : hooks) {
        try {
            current = co_await entry.hook(std::move(current));
        } catch (const std::exception& e) {
            LOG_WARN("Hook '{}' threw exception: {}", entry.name, e.what());
            // Continue with the current value; do not propagate hook failures.
        }
    }
    co_return current;
}

// -- Public run methods --

auto HookRegistry::run_before(std::string_view method, json ctx)
    -> awaitable<json> {
    auto hooks = collect_hooks(before_hooks_, method);
    if (hooks.empty()) co_return ctx;
    co_return co_await run_chain(hooks, std::move(ctx));
}

auto HookRegistry::run_after(std::string_view method, json ctx)
    -> awaitable<json> {
    auto hooks = collect_hooks(after_hooks_, method);
    if (hooks.empty()) co_return ctx;
    co_return co_await run_chain(hooks, std::move(ctx));
}

// -- Counting --

auto HookRegistry::before_count(std::string_view method) const -> size_t {
    size_t count = 0;
    if (auto it = before_hooks_.find("*"); it != before_hooks_.end()) {
        count += it->second.size();
    }
    if (auto it = before_hooks_.find(std::string(method)); it != before_hooks_.end()) {
        count += it->second.size();
    }
    return count;
}

auto HookRegistry::after_count(std::string_view method) const -> size_t {
    size_t count = 0;
    if (auto it = after_hooks_.find("*"); it != after_hooks_.end()) {
        count += it->second.size();
    }
    if (auto it = after_hooks_.find(std::string(method)); it != after_hooks_.end()) {
        count += it->second.size();
    }
    return count;
}

// -- Clear --

void HookRegistry::clear() {
    before_hooks_.clear();
    after_hooks_.clear();
}

} // namespace openclaw::gateway
