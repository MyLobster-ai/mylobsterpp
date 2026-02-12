#include "openclaw/channels/registry.hpp"
#include "openclaw/core/logger.hpp"

namespace openclaw::channels {

void ChannelRegistry::register_channel(std::unique_ptr<Channel> channel) {
    if (!channel) {
        LOG_WARN("Attempted to register null channel");
        return;
    }

    std::string channel_name{channel->name()};
    LOG_INFO("Registering channel: {} (type={})", channel_name, channel->type());

    // Apply global callback if one is set
    if (global_callback_) {
        channel->set_on_message(global_callback_);
    }

    std::lock_guard lock(mutex_);
    channels_[std::move(channel_name)] = std::move(channel);
}

auto ChannelRegistry::unregister_channel(std::string_view name) -> std::unique_ptr<Channel> {
    std::lock_guard lock(mutex_);
    auto it = channels_.find(std::string(name));
    if (it == channels_.end()) {
        return nullptr;
    }
    auto channel = std::move(it->second);
    channels_.erase(it);
    LOG_INFO("Unregistered channel: {}", name);
    return channel;
}

auto ChannelRegistry::get(std::string_view name) -> Channel* {
    std::lock_guard lock(mutex_);
    auto it = channels_.find(std::string(name));
    if (it == channels_.end()) {
        return nullptr;
    }
    return it->second.get();
}

auto ChannelRegistry::get(std::string_view name) const -> const Channel* {
    std::lock_guard lock(mutex_);
    auto it = channels_.find(std::string(name));
    if (it == channels_.end()) {
        return nullptr;
    }
    return it->second.get();
}

auto ChannelRegistry::list() const -> std::vector<std::string_view> {
    std::lock_guard lock(mutex_);
    std::vector<std::string_view> names;
    names.reserve(channels_.size());
    for (const auto& [name, _] : channels_) {
        names.emplace_back(name);
    }
    return names;
}

auto ChannelRegistry::size() const -> size_t {
    std::lock_guard lock(mutex_);
    return channels_.size();
}

auto ChannelRegistry::empty() const -> bool {
    std::lock_guard lock(mutex_);
    return channels_.empty();
}

auto ChannelRegistry::start_all() -> boost::asio::awaitable<void> {
    // Collect channel pointers under lock, then start outside lock
    std::vector<Channel*> to_start;
    {
        std::lock_guard lock(mutex_);
        to_start.reserve(channels_.size());
        for (auto& [name, ch] : channels_) {
            to_start.push_back(ch.get());
        }
    }

    for (auto* ch : to_start) {
        LOG_INFO("Starting channel: {} (type={})", ch->name(), ch->type());
        try {
            co_await ch->start();
            LOG_INFO("Channel started: {}", ch->name());
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to start channel {}: {}", ch->name(), e.what());
        }
    }
}

auto ChannelRegistry::stop_all() -> boost::asio::awaitable<void> {
    std::vector<Channel*> to_stop;
    {
        std::lock_guard lock(mutex_);
        to_stop.reserve(channels_.size());
        for (auto& [name, ch] : channels_) {
            to_stop.push_back(ch.get());
        }
    }

    for (auto* ch : to_stop) {
        LOG_INFO("Stopping channel: {}", ch->name());
        try {
            co_await ch->stop();
            LOG_INFO("Channel stopped: {}", ch->name());
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to stop channel {}: {}", ch->name(), e.what());
        }
    }
}

void ChannelRegistry::set_global_on_message(Channel::MessageCallback cb) {
    std::lock_guard lock(mutex_);
    global_callback_ = cb;
    for (auto& [name, channel] : channels_) {
        channel->set_on_message(global_callback_);
    }
}

} // namespace openclaw::channels
