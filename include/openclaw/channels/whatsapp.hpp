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

/// Configuration for the WhatsApp Cloud API channel.
struct WhatsAppConfig {
    std::string access_token;
    std::string phone_number_id;
    std::string verify_token;         // for webhook verification
    std::string channel_name = "whatsapp";
    std::string api_version = "v21.0";
    std::optional<std::string> business_account_id;
    uint16_t webhook_port = 0;        // 0 = no local webhook server
};

/// WhatsApp Cloud API channel implementation.
/// Receives messages via webhooks and sends via the Cloud API.
class WhatsAppChannel : public Channel {
public:
    WhatsAppChannel(WhatsAppConfig config, boost::asio::io_context& ioc);
    ~WhatsAppChannel() override = default;

    auto start() -> boost::asio::awaitable<void> override;
    auto stop() -> boost::asio::awaitable<void> override;
    auto send(OutgoingMessage msg) -> boost::asio::awaitable<openclaw::Result<void>> override;

    [[nodiscard]] auto name() const -> std::string_view override { return config_.channel_name; }
    [[nodiscard]] auto type() const -> std::string_view override { return "whatsapp"; }
    [[nodiscard]] auto is_running() const noexcept -> bool override { return running_.load(); }

    /// Handles an incoming webhook payload from WhatsApp.
    /// Called by an external HTTP server when a webhook event arrives.
    auto handle_webhook(const json& payload) -> void;

    /// Verifies a webhook verification request.
    /// Returns the challenge string if verification succeeds.
    [[nodiscard]] auto verify_webhook(std::string_view mode,
                                       std::string_view token,
                                       std::string_view challenge) const
        -> std::optional<std::string>;

private:
    /// Processes a WhatsApp webhook notification entry.
    auto process_entry(const json& entry) -> void;

    /// Processes a single change within an entry.
    auto process_change(const json& change) -> void;

    /// Parses a WhatsApp message into an IncomingMessage.
    auto parse_message(const json& message, const json& metadata) -> IncomingMessage;

    /// Sends a text message via the Cloud API.
    auto send_text_message(std::string_view to,
                           std::string_view text)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Sends a media message (image, document, etc.) via the Cloud API.
    auto send_media_message(std::string_view to,
                            std::string_view media_type,
                            std::string_view url,
                            std::optional<std::string_view> caption = std::nullopt)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Marks a message as read via the Cloud API.
    auto mark_as_read(std::string_view message_id)
        -> boost::asio::awaitable<openclaw::Result<void>>;

    /// Sends a reaction to a message.
    auto send_reaction(std::string_view message_id,
                       std::string_view emoji)
        -> boost::asio::awaitable<openclaw::Result<void>>;

    WhatsAppConfig config_;
    boost::asio::io_context& ioc_;
    infra::HttpClient http_;
    std::atomic<bool> running_{false};
};

/// Creates a WhatsAppChannel from generic ChannelConfig settings JSON.
auto make_whatsapp_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>;

} // namespace openclaw::channels
