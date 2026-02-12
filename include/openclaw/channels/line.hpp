#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/channels/channel.hpp"
#include "openclaw/infra/http_client.hpp"

namespace openclaw::channels {

using json = nlohmann::json;

/// Configuration for the LINE Messaging API channel.
struct LineConfig {
    std::string channel_access_token;
    std::string channel_secret;
    std::string channel_name = "line";
    uint16_t webhook_port = 0;  // 0 = no local webhook server
};

/// LINE Messaging API channel implementation.
/// Receives messages via webhooks and sends via the Messaging API.
class LineChannel : public Channel {
public:
    LineChannel(LineConfig config, boost::asio::io_context& ioc);
    ~LineChannel() override = default;

    auto start() -> boost::asio::awaitable<void> override;
    auto stop() -> boost::asio::awaitable<void> override;
    auto send(OutgoingMessage msg) -> boost::asio::awaitable<openclaw::Result<void>> override;

    [[nodiscard]] auto name() const -> std::string_view override { return config_.channel_name; }
    [[nodiscard]] auto type() const -> std::string_view override { return "line"; }
    [[nodiscard]] auto is_running() const noexcept -> bool override { return running_.load(); }

    /// Handles an incoming webhook payload from LINE.
    /// Called by an external HTTP server when a webhook event arrives.
    auto handle_webhook(const json& payload) -> void;

    /// Validates the webhook signature using the channel secret.
    [[nodiscard]] auto verify_signature(std::string_view body,
                                         std::string_view signature) const -> bool;

private:
    /// Processes a single LINE webhook event.
    auto process_event(const json& event) -> void;

    /// Parses a LINE message event into an IncomingMessage.
    auto parse_message(const json& event) -> IncomingMessage;

    /// Sends a reply message via the LINE Reply API.
    auto reply_message(std::string_view reply_token,
                       const json& messages)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Sends a push message via the LINE Push API.
    auto push_message(std::string_view to,
                      const json& messages)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Builds a text message object for the LINE API.
    static auto make_text_message(std::string_view text) -> json;

    /// Builds an image message object for the LINE API.
    static auto make_image_message(std::string_view original_url,
                                    std::string_view preview_url) -> json;

    /// Builds a file/document message object for the LINE API (via Flex Message).
    static auto make_file_message(std::string_view url,
                                   std::string_view filename) -> json;

    /// Fetches the content of a message (for retrieving images, files, etc.).
    auto get_message_content(std::string_view message_id)
        -> boost::asio::awaitable<openclaw::Result<std::string>>;

    /// Gets the user profile for a given user ID.
    auto get_user_profile(std::string_view user_id)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    LineConfig config_;
    boost::asio::io_context& ioc_;
    infra::HttpClient http_;
    infra::HttpClient data_http_;  // for data.line-svc.net (content download)
    std::atomic<bool> running_{false};
};

/// Creates a LineChannel from generic ChannelConfig settings JSON.
auto make_line_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>;

} // namespace openclaw::channels
