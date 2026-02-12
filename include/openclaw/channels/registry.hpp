#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>

#include "openclaw/channels/channel.hpp"

namespace openclaw::channels {

/// Registry that owns and manages all active channel instances.
/// Channels are registered by name and can be started/stopped collectively.
class ChannelRegistry {
public:
    ChannelRegistry() = default;
    ~ChannelRegistry() = default;

    ChannelRegistry(const ChannelRegistry&) = delete;
    ChannelRegistry& operator=(const ChannelRegistry&) = delete;

    /// Registers a channel instance. The registry takes ownership.
    /// If a channel with the same name already exists, it is replaced.
    void register_channel(std::unique_ptr<Channel> channel);

    /// Removes a channel by name. Returns the removed channel, or nullptr.
    auto unregister_channel(std::string_view name) -> std::unique_ptr<Channel>;

    /// Returns a non-owning pointer to the channel with the given name,
    /// or nullptr if not found.
    [[nodiscard]] auto get(std::string_view name) -> Channel*;

    /// Returns a non-owning pointer to the channel with the given name (const).
    [[nodiscard]] auto get(std::string_view name) const -> const Channel*;

    /// Returns the names of all registered channels.
    [[nodiscard]] auto list() const -> std::vector<std::string_view>;

    /// Returns the number of registered channels.
    [[nodiscard]] auto size() const -> size_t;

    /// Returns true if no channels are registered.
    [[nodiscard]] auto empty() const -> bool;

    /// Starts all registered channels.
    auto start_all() -> boost::asio::awaitable<void>;

    /// Stops all registered channels.
    auto stop_all() -> boost::asio::awaitable<void>;

    /// Sets a message callback that will be applied to all registered
    /// (and future) channels. Incoming messages from any channel will
    /// be routed through this callback.
    void set_global_on_message(Channel::MessageCallback cb);

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<Channel>> channels_;
    Channel::MessageCallback global_callback_;
};

} // namespace openclaw::channels
