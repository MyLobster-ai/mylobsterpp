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

/// Configuration for the Slack channel.
struct SlackConfig {
    std::string bot_token;       // xoxb-...
    std::string app_token;       // xapp-... (for Socket Mode)
    std::string channel_name = "slack";
    std::optional<std::string> signing_secret;  // for Events API verification
    bool use_socket_mode = true;  // Socket Mode (default) vs Events API
};

/// Slack channel implementation.
/// Connects via Socket Mode (WebSocket) for receiving events
/// and uses the Web API for sending messages.
class SlackChannel : public Channel {
public:
    SlackChannel(SlackConfig config, boost::asio::io_context& ioc);
    ~SlackChannel() override = default;

    auto start() -> boost::asio::awaitable<void> override;
    auto stop() -> boost::asio::awaitable<void> override;
    auto send(OutgoingMessage msg) -> boost::asio::awaitable<openclaw::Result<void>> override;

    [[nodiscard]] auto name() const -> std::string_view override { return config_.channel_name; }
    [[nodiscard]] auto type() const -> std::string_view override { return "slack"; }
    [[nodiscard]] auto is_running() const noexcept -> bool override { return running_.load(); }

private:
    /// Opens a Socket Mode WebSocket connection.
    auto open_socket_mode() -> boost::asio::awaitable<openclaw::Result<std::string>>;

    /// Socket Mode WebSocket event loop.
    auto socket_mode_loop() -> boost::asio::awaitable<void>;

    /// Handles an incoming Socket Mode envelope.
    auto handle_socket_event(const json& envelope) -> void;

    /// Processes a message event from Slack.
    auto handle_message_event(const json& event) -> void;

    /// Parses a Slack message event into an IncomingMessage.
    auto parse_message(const json& event) -> IncomingMessage;

    /// Sends a message via chat.postMessage Web API.
    auto post_message(std::string_view channel_id,
                      std::string_view text,
                      std::optional<std::string_view> thread_ts = std::nullopt)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Uploads a file to Slack via files.uploadV2.
    auto upload_file(std::string_view channel_id,
                     std::string_view url,
                     std::optional<std::string_view> filename = std::nullopt,
                     std::optional<std::string_view> title = std::nullopt)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Sends an acknowledgment for a Socket Mode envelope.
    auto send_socket_ack(std::string_view envelope_id) -> void;

    /// Fetches bot identity via auth.test.
    auto fetch_bot_info() -> boost::asio::awaitable<openclaw::Result<void>>;

    SlackConfig config_;
    boost::asio::io_context& ioc_;
    infra::HttpClient http_;
    std::atomic<bool> running_{false};
    std::string bot_user_id_;
    std::string bot_name_;
    std::string team_id_;

    // Socket Mode state
    std::string socket_url_;
    // WebSocket managed internally during socket_mode_loop
};

/// Creates a SlackChannel from generic ChannelConfig settings JSON.
auto make_slack_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>;

} // namespace openclaw::channels
