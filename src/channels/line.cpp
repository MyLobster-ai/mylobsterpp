#include "openclaw/channels/line.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <openssl/hmac.h>
#include <openssl/evp.h>

namespace openclaw::channels {

// LINE API constants
static constexpr std::string_view LINE_API_BASE = "https://api.line.me/v2";
static constexpr std::string_view LINE_DATA_API_BASE = "https://api-data.line.me/v2";

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

LineChannel::LineChannel(LineConfig config, boost::asio::io_context& ioc)
    : config_(std::move(config))
    , ioc_(ioc)
    , http_(ioc, infra::HttpClientConfig{
          .base_url = std::string(LINE_API_BASE),
          .timeout_seconds = 30,
          .default_headers = {
              {"Authorization", "Bearer " + config_.channel_access_token},
              {"Content-Type", "application/json"},
          },
      })
    , data_http_(ioc, infra::HttpClientConfig{
          .base_url = std::string(LINE_DATA_API_BASE),
          .timeout_seconds = 60,
          .default_headers = {
              {"Authorization", "Bearer " + config_.channel_access_token},
          },
      })
{
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

auto LineChannel::start() -> boost::asio::awaitable<void> {
    LOG_INFO("[line] Starting channel '{}'", config_.channel_name);
    running_.store(true);
    // LINE uses webhooks. An external HTTP server must forward webhook events
    // to handle_webhook().
    LOG_INFO("[line] Channel started - awaiting webhook events");
    co_return;
}

auto LineChannel::stop() -> boost::asio::awaitable<void> {
    LOG_INFO("[line] Stopping channel '{}'", config_.channel_name);
    running_.store(false);
    co_return;
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

auto LineChannel::send(OutgoingMessage msg)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    if (!running_.load()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError, "LINE channel is not running"));
    }

    // Build LINE messages array
    json messages = json::array();

    // Text message
    if (!msg.text.empty()) {
        messages.push_back(make_text_message(msg.text));
    }

    // Attachment messages
    for (const auto& attachment : msg.attachments) {
        if (attachment.type == "image") {
            messages.push_back(make_image_message(attachment.url, attachment.url));
        } else {
            std::string fname = attachment.filename.value_or("file");
            messages.push_back(make_file_message(attachment.url, fname));
        }
    }

    if (messages.empty()) {
        co_return ok_result();
    }

    // LINE API allows max 5 messages per request
    // Split into batches if needed
    for (size_t i = 0; i < messages.size(); i += 5) {
        json batch = json::array();
        for (size_t j = i; j < std::min(i + 5, messages.size()); ++j) {
            batch.push_back(messages[j]);
        }

        // Check if we have a reply token in the extra field
        std::string reply_token;
        if (msg.extra.contains("reply_token")) {
            reply_token = msg.extra["reply_token"].get<std::string>();
        }

        openclaw::Result<json> result;
        if (!reply_token.empty() && i == 0) {
            // Use reply API for the first batch if we have a reply token
            result = co_await reply_message(reply_token, batch);
        } else {
            // Use push API for subsequent batches or when no reply token
            result = co_await push_message(msg.recipient_id, batch);
        }

        if (!result) {
            co_return make_fail(result.error());
        }
    }

    co_return ok_result();
}

// ---------------------------------------------------------------------------
// Webhook handling
// ---------------------------------------------------------------------------

auto LineChannel::handle_webhook(const json& payload) -> void {
    if (!running_.load()) {
        return;
    }

    if (!payload.contains("events") || !payload["events"].is_array()) {
        return;
    }

    for (const auto& event : payload["events"]) {
        process_event(event);
    }
}

auto LineChannel::verify_signature(std::string_view body,
                                    std::string_view signature) const -> bool
{
    // HMAC-SHA256 of the body using the channel secret
    unsigned char hmac_result[EVP_MAX_MD_SIZE];
    unsigned int hmac_len = 0;

    HMAC(EVP_sha256(),
         config_.channel_secret.data(), static_cast<int>(config_.channel_secret.size()),
         reinterpret_cast<const unsigned char*>(body.data()), body.size(),
         hmac_result, &hmac_len);

    // Base64 encode the HMAC result
    std::string computed = utils::base64_encode(
        std::string_view(reinterpret_cast<const char*>(hmac_result), hmac_len));

    return computed == signature;
}

auto LineChannel::process_event(const json& event) -> void {
    std::string event_type = event.value("type", "");

    if (event_type == "message") {
        auto incoming = parse_message(event);
        dispatch(std::move(incoming));
    } else if (event_type == "follow") {
        LOG_INFO("[line] User followed: {}", event["source"].value("userId", ""));
    } else if (event_type == "unfollow") {
        LOG_INFO("[line] User unfollowed: {}", event["source"].value("userId", ""));
    } else if (event_type == "join") {
        LOG_INFO("[line] Bot joined group: {}", event["source"].value("groupId", ""));
    } else if (event_type == "leave") {
        LOG_INFO("[line] Bot left group: {}", event["source"].value("groupId", ""));
    } else if (event_type == "postback") {
        // Handle postback as a message
        IncomingMessage incoming;
        incoming.id = event.value("webhookEventId", utils::generate_id());
        incoming.channel = config_.channel_name;
        incoming.received_at = Clock::now();
        incoming.raw = event;

        if (event.contains("source")) {
            incoming.sender_id = event["source"].value("userId", "");
        }
        incoming.sender_name = incoming.sender_id;

        if (event.contains("postback")) {
            incoming.text = event["postback"].value("data", "");
        }

        // Store reply token
        incoming.raw["_reply_token"] = event.value("replyToken", "");

        dispatch(std::move(incoming));
    } else {
        LOG_TRACE("[line] Ignoring event type: {}", event_type);
    }
}

auto LineChannel::parse_message(const json& event) -> IncomingMessage {
    IncomingMessage incoming;
    incoming.channel = config_.channel_name;
    incoming.received_at = Clock::now();
    incoming.raw = event;

    // Event ID
    incoming.id = event.value("webhookEventId", "");
    if (incoming.id.empty() && event.contains("message")) {
        incoming.id = event["message"].value("id", utils::generate_id());
    }

    // Sender
    if (event.contains("source")) {
        const auto& source = event["source"];
        std::string source_type = source.value("type", "");
        if (source_type == "user") {
            incoming.sender_id = source.value("userId", "");
        } else if (source_type == "group") {
            incoming.sender_id = source.value("userId", "");
            incoming.thread_id = source.value("groupId", "");
        } else if (source_type == "room") {
            incoming.sender_id = source.value("userId", "");
            incoming.thread_id = source.value("roomId", "");
        }
    }
    incoming.sender_name = incoming.sender_id;  // Name requires separate API call

    // Store reply token for sending replies
    incoming.raw["_reply_token"] = event.value("replyToken", "");

    // Parse message content
    if (!event.contains("message")) {
        return incoming;
    }

    const auto& message = event["message"];
    std::string msg_type = message.value("type", "");

    if (msg_type == "text") {
        incoming.text = message.value("text", "");

        // Check for emojis in LINE's custom emoji format
        if (message.contains("emojis") && message["emojis"].is_array()) {
            incoming.raw["_emojis"] = message["emojis"];
        }

        // Mentioned users
        if (message.contains("mention") && message["mention"].contains("mentionees")) {
            incoming.raw["_mentions"] = message["mention"]["mentionees"];
        }
    } else if (msg_type == "image") {
        Attachment att;
        att.type = "image";
        att.url = message.value("id", "");  // message ID, use content API to download
        if (message.contains("contentProvider")) {
            if (message["contentProvider"].value("type", "") == "external") {
                att.url = message["contentProvider"].value("originalContentUrl", att.url);
            }
        }
        incoming.attachments.push_back(std::move(att));
    } else if (msg_type == "video") {
        Attachment att;
        att.type = "video";
        att.url = message.value("id", "");
        if (message.contains("contentProvider")) {
            if (message["contentProvider"].value("type", "") == "external") {
                att.url = message["contentProvider"].value("originalContentUrl", att.url);
            }
        }
        incoming.attachments.push_back(std::move(att));
    } else if (msg_type == "audio") {
        Attachment att;
        att.type = "audio";
        att.url = message.value("id", "");
        if (message.contains("contentProvider")) {
            if (message["contentProvider"].value("type", "") == "external") {
                att.url = message["contentProvider"].value("originalContentUrl", att.url);
            }
        }
        incoming.attachments.push_back(std::move(att));
    } else if (msg_type == "file") {
        Attachment att;
        att.type = "file";
        att.url = message.value("id", "");
        att.filename = message.value("fileName", "");
        att.size = message.value("fileSize", size_t{0});
        incoming.attachments.push_back(std::move(att));
    } else if (msg_type == "location") {
        double lat = message.value("latitude", 0.0);
        double lon = message.value("longitude", 0.0);
        incoming.text = "Location: " + std::to_string(lat) + ", " + std::to_string(lon);
        std::string title = message.value("title", "");
        if (!title.empty()) {
            incoming.text += " (" + title + ")";
        }
        std::string address = message.value("address", "");
        if (!address.empty()) {
            incoming.text += " - " + address;
        }
    } else if (msg_type == "sticker") {
        incoming.text = "[Sticker: " +
                        message.value("packageId", "") + "/" +
                        message.value("stickerId", "") + "]";
        // Sticker keywords
        if (message.contains("keywords") && message["keywords"].is_array()) {
            incoming.raw["_sticker_keywords"] = message["keywords"];
        }
    } else {
        incoming.text = "[Unsupported message type: " + msg_type + "]";
    }

    return incoming;
}

// ---------------------------------------------------------------------------
// LINE API helpers
// ---------------------------------------------------------------------------

auto LineChannel::reply_message(std::string_view reply_token, const json& messages)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    json payload = {
        {"replyToken", std::string(reply_token)},
        {"messages", messages},
    };

    auto response = co_await http_.post("/bot/message/reply", payload.dump());
    if (!response) {
        co_return make_fail(response.error());
    }
    if (!response->is_success()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError,
                       "LINE reply_message failed",
                       "status=" + std::to_string(response->status) + " body=" + response->body));
    }

    // LINE reply API returns empty body on success (200)
    co_return json::object();
}

auto LineChannel::push_message(std::string_view to, const json& messages)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    json payload = {
        {"to", std::string(to)},
        {"messages", messages},
    };

    auto response = co_await http_.post("/bot/message/push", payload.dump());
    if (!response) {
        co_return make_fail(response.error());
    }
    if (!response->is_success()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError,
                       "LINE push_message failed",
                       "status=" + std::to_string(response->status) + " body=" + response->body));
    }

    co_return json::object();
}

auto LineChannel::make_text_message(std::string_view text) -> json {
    return json{
        {"type", "text"},
        {"text", std::string(text)},
    };
}

auto LineChannel::make_image_message(std::string_view original_url,
                                      std::string_view preview_url) -> json
{
    return json{
        {"type", "image"},
        {"originalContentUrl", std::string(original_url)},
        {"previewImageUrl", std::string(preview_url)},
    };
}

auto LineChannel::make_file_message(std::string_view url,
                                     std::string_view filename) -> json
{
    // LINE doesn't have a native file message type for all file types.
    // Use a Flex Message with a link to the file.
    return json{
        {"type", "flex"},
        {"altText", std::string(filename)},
        {"contents", {
            {"type", "bubble"},
            {"body", {
                {"type", "box"},
                {"layout", "vertical"},
                {"contents", json::array({
                    {
                        {"type", "text"},
                        {"text", std::string(filename)},
                        {"weight", "bold"},
                        {"size", "md"},
                    },
                    {
                        {"type", "button"},
                        {"action", {
                            {"type", "uri"},
                            {"label", "Download"},
                            {"uri", std::string(url)},
                        }},
                        {"style", "primary"},
                        {"margin", "md"},
                    },
                })},
            }},
        }},
    };
}

auto LineChannel::get_message_content(std::string_view message_id)
    -> boost::asio::awaitable<openclaw::Result<std::string>>
{
    std::string path = "/bot/message/" + std::string(message_id) + "/content";
    auto response = co_await data_http_.get(path);
    if (!response) {
        co_return make_fail(response.error());
    }
    if (!response->is_success()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError,
                       "Failed to get message content",
                       "status=" + std::to_string(response->status)));
    }

    co_return response->body;
}

auto LineChannel::get_user_profile(std::string_view user_id)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    std::string path = "/bot/profile/" + std::string(user_id);
    auto response = co_await http_.get(path);
    if (!response) {
        co_return make_fail(response.error());
    }
    if (!response->is_success()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError,
                       "Failed to get user profile",
                       "status=" + std::to_string(response->status)));
    }

    try {
        co_return json::parse(response->body);
    } catch (const json::exception& e) {
        co_return make_fail(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse user profile", e.what()));
    }
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

auto make_line_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>
{
    LineConfig config;
    config.channel_access_token = settings.value("channel_access_token", "");
    config.channel_secret = settings.value("channel_secret", "");
    config.channel_name = settings.value("channel_name", "line");
    config.webhook_port = settings.value("webhook_port", static_cast<uint16_t>(0));

    if (config.channel_access_token.empty()) {
        LOG_ERROR("[line] channel_access_token is required in channel settings");
        return nullptr;
    }
    if (config.channel_secret.empty()) {
        LOG_ERROR("[line] channel_secret is required in channel settings");
        return nullptr;
    }

    return std::make_unique<LineChannel>(std::move(config), ioc);
}

} // namespace openclaw::channels
