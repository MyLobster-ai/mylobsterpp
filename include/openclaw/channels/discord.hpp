#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/channels/channel.hpp"
#include "openclaw/infra/http_client.hpp"

namespace openclaw::channels {

using json = nlohmann::json;

/// Configuration for the Discord channel.
struct DiscordConfig {
    std::string bot_token;
    std::string channel_name = "discord";
    int intents = 33281;  // GUILDS | GUILD_MESSAGES | DIRECT_MESSAGES | MESSAGE_CONTENT
    int heartbeat_interval_ms = 41250;
    std::optional<std::string> application_id;

    // Presence configuration
    std::optional<std::string> presence_status;   // "online", "dnd", "idle", "invisible"
    std::optional<std::string> activity_name;      // Activity display name
    std::optional<int> activity_type;             // 0=Game, 1=Streaming, 2=Listening, 3=Watching, 5=Competing
    std::optional<std::string> activity_url;       // Streaming URL (only for type 1)

    // AutoThread configuration
    bool auto_thread = false;                      // Auto-create threads for replies
    int auto_thread_ttl_minutes = 5;              // Thread starter cache TTL
};

/// Discord channel implementation.
/// Connects to the Discord Gateway (WebSocket) for receiving events
/// and uses the REST API for sending messages.
class DiscordChannel : public Channel {
public:
    DiscordChannel(DiscordConfig config, boost::asio::io_context& ioc);
    ~DiscordChannel() override = default;

    auto start() -> boost::asio::awaitable<void> override;
    auto stop() -> boost::asio::awaitable<void> override;
    auto send(OutgoingMessage msg) -> boost::asio::awaitable<openclaw::Result<void>> override;

    [[nodiscard]] auto name() const -> std::string_view override { return config_.channel_name; }
    [[nodiscard]] auto type() const -> std::string_view override { return "discord"; }
    [[nodiscard]] auto is_running() const noexcept -> bool override { return running_.load(); }

private:
    /// Fetches the WebSocket gateway URL from Discord REST API.
    auto fetch_gateway_url() -> boost::asio::awaitable<openclaw::Result<std::string>>;

    /// WebSocket connection and event loop.
    auto gateway_loop() -> boost::asio::awaitable<void>;

    /// Sends the IDENTIFY payload after connecting to the gateway.
    auto send_identify() -> boost::asio::awaitable<void>;

    /// Sends a heartbeat to the gateway.
    auto send_heartbeat() -> boost::asio::awaitable<void>;

    /// Heartbeat loop that sends periodic heartbeats.
    auto heartbeat_loop() -> boost::asio::awaitable<void>;

    /// Handles a RESUME after disconnection.
    auto send_resume() -> boost::asio::awaitable<void>;

    /// Dispatches a gateway event.
    auto handle_dispatch(const json& payload) -> void;

    /// Processes a MESSAGE_CREATE event.
    auto handle_message_create(const json& data) -> void;

    /// Parses a Discord message into an IncomingMessage.
    auto parse_message(const json& msg) -> IncomingMessage;

    /// Sends a message to a Discord channel via REST.
    auto send_channel_message(std::string_view channel_id,
                              std::string_view content,
                              std::optional<std::string_view> reply_to = std::nullopt)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Sends a file to a Discord channel via REST.
    auto send_channel_file(std::string_view channel_id,
                           std::string_view url,
                           std::optional<std::string_view> caption = std::nullopt)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Sends a voice message via 3-step CDN upload.
    auto send_voice_message(std::string_view channel_id,
                            std::string_view audio_data,
                            std::string_view waveform_b64)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Creates a thread from a message.
    auto create_thread(std::string_view channel_id,
                       std::string_view message_id,
                       std::string_view name)
        -> boost::asio::awaitable<openclaw::Result<json>>;

public:
    /// Generates a waveform from PCM audio data (256 amplitude samples, base64).
    static auto generate_waveform(const std::vector<uint8_t>& pcm_data) -> std::string;

private:
    DiscordConfig config_;
    boost::asio::io_context& ioc_;
    infra::HttpClient http_;
    std::atomic<bool> running_{false};
    std::string gateway_url_;
    std::string session_id_;
    std::optional<int> last_sequence_;
    std::string bot_user_id_;
    boost::asio::steady_timer heartbeat_timer_;

    // WebSocket state managed internally during gateway_loop
};

/// Creates a DiscordChannel from generic ChannelConfig settings JSON.
auto make_discord_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>;

} // namespace openclaw::channels
