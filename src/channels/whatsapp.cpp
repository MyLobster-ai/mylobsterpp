#include "openclaw/channels/whatsapp.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::channels {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

WhatsAppChannel::WhatsAppChannel(WhatsAppConfig config, boost::asio::io_context& ioc)
    : config_(std::move(config))
    , ioc_(ioc)
    , http_(ioc, infra::HttpClientConfig{
          .base_url = "https://graph.facebook.com/" + config_.api_version,
          .timeout_seconds = 30,
          .default_headers = {
              {"Authorization", "Bearer " + config_.access_token},
              {"Content-Type", "application/json"},
          },
      })
{
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

auto WhatsAppChannel::start() -> boost::asio::awaitable<void> {
    LOG_INFO("[whatsapp] Starting channel '{}' (phone_number_id={})",
             config_.channel_name, config_.phone_number_id);
    running_.store(true);
    // WhatsApp Cloud API is webhook-based.
    // An external HTTP server must be configured to forward webhooks
    // to handle_webhook().
    LOG_INFO("[whatsapp] Channel started - awaiting webhook events");
    co_return;
}

auto WhatsAppChannel::stop() -> boost::asio::awaitable<void> {
    LOG_INFO("[whatsapp] Stopping channel '{}'", config_.channel_name);
    running_.store(false);
    co_return;
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

auto WhatsAppChannel::send(OutgoingMessage msg)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    if (!running_.load()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError, "WhatsApp channel is not running"));
    }

    // Send text message
    if (!msg.text.empty()) {
        auto result = co_await send_text_message(msg.recipient_id, msg.text);
        if (!result) {
            co_return std::unexpected(result.error());
        }
    }

    // Send attachments
    for (const auto& attachment : msg.attachments) {
        std::string media_type;
        if (attachment.type == "image") {
            media_type = "image";
        } else if (attachment.type == "video") {
            media_type = "video";
        } else if (attachment.type == "audio") {
            media_type = "audio";
        } else {
            media_type = "document";
        }

        auto result = co_await send_media_message(msg.recipient_id,
            media_type, attachment.url,
            attachment.filename ? std::optional<std::string_view>{*attachment.filename}
                               : std::nullopt);
        if (!result) {
            LOG_WARN("[whatsapp] Failed to send media: {}", result.error().what());
        }
    }

    co_return openclaw::Result<void>{};
}

// ---------------------------------------------------------------------------
// Webhook handling
// ---------------------------------------------------------------------------

auto WhatsAppChannel::handle_webhook(const json& payload) -> void {
    if (!running_.load()) {
        return;
    }

    // WhatsApp webhook structure:
    // { "object": "whatsapp_business_account", "entry": [...] }
    if (payload.value("object", "") != "whatsapp_business_account") {
        LOG_TRACE("[whatsapp] Ignoring non-WhatsApp webhook");
        return;
    }

    if (!payload.contains("entry") || !payload["entry"].is_array()) {
        return;
    }

    for (const auto& entry : payload["entry"]) {
        process_entry(entry);
    }
}

auto WhatsAppChannel::verify_webhook(std::string_view mode,
                                      std::string_view token,
                                      std::string_view challenge) const
    -> std::optional<std::string>
{
    if (mode == "subscribe" && token == config_.verify_token) {
        LOG_INFO("[whatsapp] Webhook verification successful");
        return std::string(challenge);
    }
    LOG_WARN("[whatsapp] Webhook verification failed: mode={} token_match={}",
             mode, token == config_.verify_token);
    return std::nullopt;
}

auto WhatsAppChannel::process_entry(const json& entry) -> void {
    if (!entry.contains("changes") || !entry["changes"].is_array()) {
        return;
    }
    for (const auto& change : entry["changes"]) {
        process_change(change);
    }
}

auto WhatsAppChannel::process_change(const json& change) -> void {
    std::string field = change.value("field", "");
    if (field != "messages") {
        return;
    }

    if (!change.contains("value")) {
        return;
    }

    const auto& value = change["value"];

    // Get metadata
    json metadata = {};
    if (value.contains("metadata")) {
        metadata = value["metadata"];
    }

    // Process messages
    if (value.contains("messages") && value["messages"].is_array()) {
        // Also extract contacts for sender names
        json contacts = {};
        if (value.contains("contacts") && value["contacts"].is_array()) {
            contacts = value["contacts"];
        }

        for (const auto& message : value["messages"]) {
            auto incoming = parse_message(message, metadata);

            // Try to find sender name from contacts
            std::string sender_wa_id = message.value("from", "");
            for (const auto& contact : contacts) {
                if (contact.value("wa_id", "") == sender_wa_id) {
                    if (contact.contains("profile")) {
                        incoming.sender_name = contact["profile"].value("name", sender_wa_id);
                    }
                    break;
                }
            }

            dispatch(std::move(incoming));
        }
    }

    // Process statuses (delivery receipts)
    if (value.contains("statuses") && value["statuses"].is_array()) {
        for (const auto& status : value["statuses"]) {
            LOG_TRACE("[whatsapp] Message status: id={} status={}",
                      status.value("id", ""), status.value("status", ""));
        }
    }
}

auto WhatsAppChannel::parse_message(const json& message, const json& metadata)
    -> IncomingMessage
{
    IncomingMessage incoming;
    incoming.id = message.value("id", "");
    incoming.channel = config_.channel_name;
    incoming.sender_id = message.value("from", "");
    incoming.sender_name = incoming.sender_id;  // may be overridden by contacts
    incoming.received_at = Clock::now();
    incoming.raw = message;
    incoming.raw["_metadata"] = metadata;

    // Context (reply-to)
    if (message.contains("context")) {
        incoming.reply_to = message["context"].value("id", "");
    }

    // Message type
    std::string msg_type = message.value("type", "");

    if (msg_type == "text") {
        incoming.text = message["text"].value("body", "");
    } else if (msg_type == "image") {
        incoming.text = message["image"].value("caption", "");
        Attachment att;
        att.type = "image";
        att.url = message["image"].value("id", "");  // media ID, needs download
        if (message["image"].contains("mime_type")) {
            att.filename = "image." + message["image"].value("mime_type", "jpeg");
        }
        incoming.attachments.push_back(std::move(att));
    } else if (msg_type == "document") {
        incoming.text = message["document"].value("caption", "");
        Attachment att;
        att.type = "file";
        att.url = message["document"].value("id", "");
        att.filename = message["document"].value("filename", "document");
        incoming.attachments.push_back(std::move(att));
    } else if (msg_type == "audio") {
        Attachment att;
        att.type = "audio";
        att.url = message["audio"].value("id", "");
        incoming.attachments.push_back(std::move(att));
    } else if (msg_type == "video") {
        incoming.text = message["video"].value("caption", "");
        Attachment att;
        att.type = "video";
        att.url = message["video"].value("id", "");
        incoming.attachments.push_back(std::move(att));
    } else if (msg_type == "sticker") {
        Attachment att;
        att.type = "image";
        att.url = message["sticker"].value("id", "");
        incoming.attachments.push_back(std::move(att));
    } else if (msg_type == "location") {
        double lat = message["location"].value("latitude", 0.0);
        double lon = message["location"].value("longitude", 0.0);
        incoming.text = "Location: " + std::to_string(lat) + ", " + std::to_string(lon);
        if (message["location"].contains("name")) {
            incoming.text += " (" + message["location"].value("name", "") + ")";
        }
    } else if (msg_type == "contacts") {
        incoming.text = "[Contact shared]";
    } else if (msg_type == "interactive") {
        // Button replies or list selections
        if (message.contains("interactive")) {
            const auto& interactive = message["interactive"];
            std::string reply_type = interactive.value("type", "");
            if (reply_type == "button_reply") {
                incoming.text = interactive["button_reply"].value("title", "");
            } else if (reply_type == "list_reply") {
                incoming.text = interactive["list_reply"].value("title", "");
            }
        }
    } else if (msg_type == "reaction") {
        // Reaction to a message
        if (message.contains("reaction")) {
            incoming.text = message["reaction"].value("emoji", "");
            incoming.reply_to = message["reaction"].value("message_id", "");
        }
    } else {
        incoming.text = "[Unsupported message type: " + msg_type + "]";
    }

    return incoming;
}

// ---------------------------------------------------------------------------
// Cloud API helpers
// ---------------------------------------------------------------------------

auto WhatsAppChannel::send_text_message(std::string_view to, std::string_view text)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    json payload = {
        {"messaging_product", "whatsapp"},
        {"recipient_type", "individual"},
        {"to", std::string(to)},
        {"type", "text"},
        {"text", {
            {"preview_url", false},
            {"body", std::string(text)},
        }},
    };

    std::string path = "/" + config_.phone_number_id + "/messages";
    auto response = co_await http_.post(path, payload.dump());
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "WhatsApp send text failed",
                       "status=" + std::to_string(response->status) + " body=" + response->body));
    }

    try {
        co_return json::parse(response->body);
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse WhatsApp response", e.what()));
    }
}

auto WhatsAppChannel::send_media_message(std::string_view to,
                                          std::string_view media_type,
                                          std::string_view url,
                                          std::optional<std::string_view> caption)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    json media_object = {
        {"link", std::string(url)},
    };
    if (caption) {
        media_object["caption"] = std::string(*caption);
    }

    json payload = {
        {"messaging_product", "whatsapp"},
        {"recipient_type", "individual"},
        {"to", std::string(to)},
        {"type", std::string(media_type)},
        {std::string(media_type), media_object},
    };

    std::string path = "/" + config_.phone_number_id + "/messages";
    auto response = co_await http_.post(path, payload.dump());
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "WhatsApp send media failed",
                       "status=" + std::to_string(response->status) + " body=" + response->body));
    }

    try {
        co_return json::parse(response->body);
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse WhatsApp response", e.what()));
    }
}

auto WhatsAppChannel::mark_as_read(std::string_view message_id)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    json payload = {
        {"messaging_product", "whatsapp"},
        {"status", "read"},
        {"message_id", std::string(message_id)},
    };

    std::string path = "/" + config_.phone_number_id + "/messages";
    auto response = co_await http_.post(path, payload.dump());
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "WhatsApp mark_as_read failed",
                       "status=" + std::to_string(response->status)));
    }

    co_return openclaw::Result<void>{};
}

auto WhatsAppChannel::send_reaction(std::string_view message_id, std::string_view emoji)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    json payload = {
        {"messaging_product", "whatsapp"},
        {"recipient_type", "individual"},
        {"type", "reaction"},
        {"reaction", {
            {"message_id", std::string(message_id)},
            {"emoji", std::string(emoji)},
        }},
    };

    std::string path = "/" + config_.phone_number_id + "/messages";
    auto response = co_await http_.post(path, payload.dump());
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "WhatsApp send_reaction failed",
                       "status=" + std::to_string(response->status)));
    }

    co_return openclaw::Result<void>{};
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

auto make_whatsapp_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>
{
    WhatsAppConfig config;
    config.access_token = settings.value("access_token", "");
    config.phone_number_id = settings.value("phone_number_id", "");
    config.verify_token = settings.value("verify_token", "");
    config.channel_name = settings.value("channel_name", "whatsapp");
    config.api_version = settings.value("api_version", "v21.0");
    if (settings.contains("business_account_id")) {
        config.business_account_id = settings.at("business_account_id").get<std::string>();
    }
    config.webhook_port = settings.value("webhook_port", static_cast<uint16_t>(0));

    if (config.access_token.empty()) {
        LOG_ERROR("[whatsapp] access_token is required in channel settings");
        return nullptr;
    }
    if (config.phone_number_id.empty()) {
        LOG_ERROR("[whatsapp] phone_number_id is required in channel settings");
        return nullptr;
    }

    return std::make_unique<WhatsAppChannel>(std::move(config), ioc);
}

} // namespace openclaw::channels
