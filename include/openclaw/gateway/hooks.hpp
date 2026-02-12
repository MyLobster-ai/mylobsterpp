#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

/// A hook function that receives JSON context and returns (possibly modified)
/// JSON.  Hooks can inspect/transform request params (before) or response
/// results (after).
using Hook = std::function<awaitable<json>(json ctx)>;

/// Priority levels for hook ordering.
enum class HookPriority : int {
    Highest = 0,
    High    = 100,
    Normal  = 500,
    Low     = 900,
    Lowest  = 1000,
};

/// A single registered hook entry with its metadata.
struct HookEntry {
    std::string name;
    Hook hook;
    HookPriority priority = HookPriority::Normal;
};

/// The HookRegistry manages before/after hooks for RPC methods.
/// Hooks are executed in priority order (lowest numeric value first).
///
/// Before hooks receive the request params and may modify them before
/// dispatch. After hooks receive the response result and may modify or
/// augment it.
///
/// Hooks can be registered for a specific method name or for the wildcard
/// "*" which applies to all methods.
class HookRegistry {
public:
    HookRegistry() = default;

    /// Register a hook to run before a specific method.
    void before(std::string_view method, std::string name, Hook hook,
                HookPriority priority = HookPriority::Normal);

    /// Register a hook to run after a specific method.
    void after(std::string_view method, std::string name, Hook hook,
               HookPriority priority = HookPriority::Normal);

    /// Register a hook to run before ALL methods (wildcard).
    void before_all(std::string name, Hook hook,
                    HookPriority priority = HookPriority::Normal);

    /// Register a hook to run after ALL methods (wildcard).
    void after_all(std::string name, Hook hook,
                   HookPriority priority = HookPriority::Normal);

    /// Remove a named before hook from a method.
    auto remove_before(std::string_view method, std::string_view name) -> bool;

    /// Remove a named after hook from a method.
    auto remove_after(std::string_view method, std::string_view name) -> bool;

    /// Execute all before hooks for a method (method-specific + wildcard).
    /// The ctx JSON is passed through each hook in sequence, and the final
    /// result is returned.
    auto run_before(std::string_view method, json ctx) -> awaitable<json>;

    /// Execute all after hooks for a method (method-specific + wildcard).
    auto run_after(std::string_view method, json ctx) -> awaitable<json>;

    /// Return the number of registered before hooks for a method
    /// (including wildcards).
    [[nodiscard]] auto before_count(std::string_view method) const -> size_t;

    /// Return the number of registered after hooks for a method
    /// (including wildcards).
    [[nodiscard]] auto after_count(std::string_view method) const -> size_t;

    /// Clear all hooks.
    void clear();

private:
    using HookList = std::vector<HookEntry>;

    std::unordered_map<std::string, HookList> before_hooks_;
    std::unordered_map<std::string, HookList> after_hooks_;

    static void insert_sorted(HookList& list, HookEntry entry);
    static auto collect_hooks(const std::unordered_map<std::string, HookList>& map,
                              std::string_view method) -> HookList;
    static auto run_chain(const HookList& hooks, json ctx) -> awaitable<json>;
};

} // namespace openclaw::gateway
