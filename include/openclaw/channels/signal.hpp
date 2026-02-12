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

/// Configuration for the Signal channel.
/// Uses signal-cli REST API as a bridge to the Signal protocol.
struct SignalConfig {
    std::string api_url = "http://localhost:8080";  // signal-cli REST API base URL
    std::string phone_number;                        // registered Signal phone number
    std::string channel_name = "signal";
    int poll_interval_ms = 1000;                     // polling interval for receive
};

/// Signal channel implementation via signal-cli REST API.
/// Receives messages by polling /v1/receive and sends via /v2/send.
class SignalChannel : public Channel {
public:
    SignalChannel(SignalConfig config, boost::asio::io_context& ioc);
    ~SignalChannel() override = default;

    auto start() -> boost::asio::awaitable<void> override;
    auto stop() -> boost::asio::awaitable<void> override;
    auto send(OutgoingMessage msg) -> boost::asio::awaitable<openclaw::Result<void>> override;

    [[nodiscard]] auto name() const -> std::string_view override { return config_.channel_name; }
    [[nodiscard]] auto type() const -> std::string_view override { return "signal"; }
    [[nodiscard]] auto is_running() const noexcept -> bool override { return running_.load(); }

private:
    /// Polling loop that fetches messages from signal-cli REST API.
    auto poll_loop() -> boost::asio::awaitable<void>;

    /// Processes a batch of received messages.
    auto process_messages(const json& messages) -> void;

    /// Parses a signal-cli message envelope into an IncomingMessage.
    auto parse_message(const json& envelope) -> std::optional<IncomingMessage>;

    /// Sends a text message via /v2/send.
    auto send_text(std::string_view recipient,
                   std::string_view text)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Sends an attachment via /v2/send with base64 data.
    auto send_attachment(std::string_view recipient,
                         std::string_view url,
                         std::string_view content_type)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Sends a reaction to a message.
    auto send_reaction(std::string_view recipient,
                       std::string_view target_author,
                       int64_t target_timestamp,
                       std::string_view emoji)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Verifies connectivity to the signal-cli REST API.
    auto verify_connection() -> boost::asio::awaitable<openclaw::Result<void>>;

    SignalConfig config_;
    boost::asio::io_context& ioc_;
    infra::HttpClient http_;
    std::atomic<bool> running_{false};
};

/// Creates a SignalChannel from generic ChannelConfig settings JSON.
auto make_signal_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>;

} // namespace openclaw::channels
