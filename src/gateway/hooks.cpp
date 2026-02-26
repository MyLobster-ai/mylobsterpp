#include "openclaw/gateway/hooks.hpp"

#include "openclaw/core/logger.hpp"

#include <algorithm>
#include <cctype>

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

// -- Webhook URL validation (v2026.2.25) --

auto HookRegistry::validate_webhook_url(std::string_view url) -> bool {
    if (url.empty()) {
        LOG_WARN("Hook URL validation: empty URL");
        return false;
    }

    // Must have a scheme
    auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        LOG_WARN("Hook URL validation: missing scheme in '{}'", url);
        return false;
    }

    auto rest = url.substr(scheme_end + 3);

    // Reject userinfo (user:pass@host)
    auto at_pos = rest.find('@');
    auto slash_pos = rest.find('/');
    if (at_pos != std::string_view::npos &&
        (slash_pos == std::string_view::npos || at_pos < slash_pos)) {
        LOG_WARN("Hook URL validation: userinfo detected in URL");
        return false;
    }

    // Extract host (before first / or end)
    auto host = (slash_pos != std::string_view::npos)
        ? rest.substr(0, slash_pos)
        : rest;

    // Strip port
    auto colon_pos = host.find(':');
    if (colon_pos != std::string_view::npos) {
        host = host.substr(0, colon_pos);
    }

    // Reject empty host
    if (host.empty()) {
        LOG_WARN("Hook URL validation: empty host");
        return false;
    }

    // Reject encoded path traversal sequences
    std::string url_lower(url);
    std::transform(url_lower.begin(), url_lower.end(), url_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (url_lower.find("%2e%2e") != std::string::npos ||  // ..
        url_lower.find("%2f") != std::string::npos ||      // /
        url_lower.find("%5c") != std::string::npos) {      // backslash
        LOG_WARN("Hook URL validation: encoded traversal detected in URL");
        return false;
    }

    return true;
}

} // namespace openclaw::gateway
