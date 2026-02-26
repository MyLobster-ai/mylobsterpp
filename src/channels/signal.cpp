#include "openclaw/channels/signal.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <boost/asio/steady_timer.hpp>

namespace openclaw::channels {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SignalChannel::SignalChannel(SignalConfig config, boost::asio::io_context& ioc)
    : config_(std::move(config))
    , ioc_(ioc)
    , http_(ioc, infra::HttpClientConfig{
          .base_url = config_.api_url,
          .timeout_seconds = 30,
          .default_headers = {
              {"Content-Type", "application/json"},
          },
      })
{
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

auto SignalChannel::start() -> boost::asio::awaitable<void> {
    LOG_INFO("[signal] Starting channel '{}' (phone={})",
             config_.channel_name, config_.phone_number);

    // Check architecture for arm64 Linux - signal-cli may need manual install
#if defined(__aarch64__) && defined(__linux__)
    LOG_WARN("[signal] Running on arm64 Linux. If signal-cli is not installed, "
             "install via Homebrew: brew install signal-cli");
#endif

    auto check = co_await verify_connection();
    if (!check) {
        LOG_ERROR("[signal] Failed to verify signal-cli connection: {}",
                  check.error().what());
#if defined(__aarch64__) && defined(__linux__)
        LOG_ERROR("[signal] On arm64 Linux, signal-cli may require manual installation. "
                  "Try: brew install signal-cli  OR  "
                  "download from https://github.com/AsamK/signal-cli/releases");
#endif
        co_return;
    }

    LOG_INFO("[signal] Connected to signal-cli REST API at {}", config_.api_url);
    running_.store(true);

    // Spawn the polling loop
    boost::asio::co_spawn(ioc_, poll_loop(), boost::asio::detached);
}

auto SignalChannel::stop() -> boost::asio::awaitable<void> {
    LOG_INFO("[signal] Stopping channel '{}'", config_.channel_name);
    running_.store(false);
    co_return;
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

auto SignalChannel::send(OutgoingMessage msg)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    if (!running_.load()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError, "Signal channel is not running"));
    }

    // Send text message
    if (!msg.text.empty()) {
        auto result = co_await send_text(msg.recipient_id, msg.text);
        if (!result) {
            co_return make_fail(result.error());
        }
    }

    // Send attachments
    for (const auto& attachment : msg.attachments) {
        std::string content_type;
        if (attachment.type == "image") {
            content_type = "image/png";
        } else if (attachment.type == "audio") {
            content_type = "audio/mpeg";
        } else if (attachment.type == "video") {
            content_type = "video/mp4";
        } else {
            content_type = "application/octet-stream";
        }

        auto result = co_await send_attachment(msg.recipient_id, attachment.url, content_type);
        if (!result) {
            LOG_WARN("[signal] Failed to send attachment: {}", result.error().what());
        }
    }

    co_return ok_result();
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

auto SignalChannel::poll_loop() -> boost::asio::awaitable<void> {
    LOG_DEBUG("[signal] Entering poll loop");

    // URL-encode the phone number for the REST API path
    std::string encoded_number = utils::url_encode(config_.phone_number);

    while (running_.load()) {
        // signal-cli REST API: GET /v1/receive/{number}
        std::string path = "/v1/receive/" + encoded_number;
        auto response = co_await http_.get(path);

        if (!response) {
            LOG_WARN("[signal] Receive poll failed: {}", response.error().what());
            boost::asio::steady_timer timer(ioc_, std::chrono::seconds(5));
            co_await timer.async_wait(boost::asio::use_awaitable);
            continue;
        }

        if (!response->is_success()) {
            LOG_WARN("[signal] Receive returned status {}: {}",
                     response->status, response->body);
            boost::asio::steady_timer timer(ioc_, std::chrono::seconds(5));
            co_await timer.async_wait(boost::asio::use_awaitable);
            continue;
        }

        try {
            auto messages = json::parse(response->body);
            if (messages.is_array() && !messages.empty()) {
                process_messages(messages);
            }
        } catch (const json::exception& e) {
            LOG_ERROR("[signal] JSON parse error: {}", e.what());
        }

        // Wait before next poll
        boost::asio::steady_timer timer(
            ioc_, std::chrono::milliseconds(config_.poll_interval_ms));
        co_await timer.async_wait(boost::asio::use_awaitable);
    }

    LOG_DEBUG("[signal] Exiting poll loop");
}

// ---------------------------------------------------------------------------
// Message processing
// ---------------------------------------------------------------------------

auto SignalChannel::process_messages(const json& messages) -> void {
    for (const auto& envelope_wrapper : messages) {
        // signal-cli REST API wraps each message in an "envelope" key
        json envelope;
        if (envelope_wrapper.contains("envelope")) {
            envelope = envelope_wrapper["envelope"];
        } else {
            envelope = envelope_wrapper;
        }

        auto incoming = parse_message(envelope);
        if (incoming) {
            dispatch(std::move(*incoming));
        }
    }
}

auto SignalChannel::parse_message(const json& envelope)
    -> std::optional<IncomingMessage>
{
    // signal-cli envelope structure:
    // { "source": "+1234567890", "sourceNumber": "...", "sourceName": "...",
    //   "timestamp": 1234567890, "dataMessage": { "message": "hello", ... } }

    if (!envelope.contains("dataMessage")) {
        // Not a data message (could be receipt, typing indicator, etc.)
        return std::nullopt;
    }

    const auto& data_msg = envelope["dataMessage"];

    IncomingMessage incoming;
    incoming.channel = config_.channel_name;
    incoming.received_at = Clock::now();
    incoming.raw = envelope;

    // Message ID from timestamp
    int64_t timestamp = data_msg.value("timestamp", envelope.value("timestamp", int64_t{0}));
    incoming.id = std::to_string(timestamp);

    // Sender
    incoming.sender_id = envelope.value("sourceNumber",
                                         envelope.value("source", ""));
    incoming.sender_name = envelope.value("sourceName", incoming.sender_id);

    // Text
    incoming.text = data_msg.value("message", "");

    // Quote (reply-to)
    if (data_msg.contains("quote")) {
        incoming.reply_to = std::to_string(
            data_msg["quote"].value("id", int64_t{0}));
    }

    // Group message (thread)
    if (data_msg.contains("groupInfo")) {
        incoming.thread_id = data_msg["groupInfo"].value("groupId", "");
    }

    // Attachments
    if (data_msg.contains("attachments") && data_msg["attachments"].is_array()) {
        for (const auto& att_json : data_msg["attachments"]) {
            Attachment att;
            att.url = att_json.value("id", "");  // attachment ID for retrieval
            att.filename = att_json.value("filename", "");
            att.size = att_json.value("size", size_t{0});

            std::string content_type = att_json.value("contentType", "");
            if (content_type.starts_with("image/")) {
                att.type = "image";
            } else if (content_type.starts_with("video/")) {
                att.type = "video";
            } else if (content_type.starts_with("audio/")) {
                att.type = "audio";
            } else {
                att.type = "file";
            }

            incoming.attachments.push_back(std::move(att));
        }
    }

    // Sticker
    if (data_msg.contains("sticker")) {
        Attachment att;
        att.type = "image";
        att.url = data_msg["sticker"].value("packId", "") + ":" +
                  std::to_string(data_msg["sticker"].value("stickerId", 0));
        incoming.attachments.push_back(std::move(att));
    }

    // If no text and no attachments, skip
    if (incoming.text.empty() && incoming.attachments.empty()) {
        return std::nullopt;
    }

    return incoming;
}

// ---------------------------------------------------------------------------
// API helpers
// ---------------------------------------------------------------------------

auto SignalChannel::send_text(std::string_view recipient, std::string_view text)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    json payload = {
        {"message", std::string(text)},
        {"number", config_.phone_number},
        {"recipients", json::array({std::string(recipient)})},
    };

    auto response = co_await http_.post("/v2/send", payload.dump());
    if (!response) {
        co_return make_fail(response.error());
    }
    if (!response->is_success()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError,
                       "Signal send failed",
                       "status=" + std::to_string(response->status) + " body=" + response->body));
    }

    try {
        co_return json::parse(response->body);
    } catch (const json::exception& e) {
        co_return make_fail(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse Signal send response", e.what()));
    }
}

auto SignalChannel::send_attachment(std::string_view recipient,
                                     std::string_view url,
                                     std::string_view content_type)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    // signal-cli REST API expects base64-encoded attachment data in the payload.
    // For URL-based attachments, we include the URL as a text message with a note.
    // A full implementation would fetch the URL, base64 encode it, and attach it.
    json payload = {
        {"message", std::string(url)},
        {"number", config_.phone_number},
        {"recipients", json::array({std::string(recipient)})},
        {"base64_attachments", json::array()},
    };

    // If a real file were provided, it would be base64-encoded here:
    // payload["base64_attachments"].push_back(base64_data_with_content_type);

    auto response = co_await http_.post("/v2/send", payload.dump());
    if (!response) {
        co_return make_fail(response.error());
    }
    if (!response->is_success()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError,
                       "Signal send attachment failed",
                       "status=" + std::to_string(response->status)));
    }

    try {
        co_return json::parse(response->body);
    } catch (const json::exception& e) {
        co_return make_fail(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse Signal response", e.what()));
    }
}

auto SignalChannel::send_reaction(std::string_view recipient,
                                   std::string_view target_author,
                                   int64_t target_timestamp,
                                   std::string_view emoji)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    json payload = {
        {"recipient", std::string(recipient)},
        {"reaction", {
            {"emoji", std::string(emoji)},
            {"target_author", std::string(target_author)},
            {"timestamp", target_timestamp},
        }},
        {"number", config_.phone_number},
    };

    std::string encoded_number = utils::url_encode(config_.phone_number);
    std::string path = "/v1/reactions/" + encoded_number;
    auto response = co_await http_.post(path, payload.dump());
    if (!response) {
        co_return make_fail(response.error());
    }
    if (!response->is_success()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError,
                       "Signal send reaction failed",
                       "status=" + std::to_string(response->status)));
    }

    try {
        co_return json::parse(response->body);
    } catch (const json::exception& e) {
        co_return make_fail(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse Signal response", e.what()));
    }
}

auto SignalChannel::verify_connection() -> boost::asio::awaitable<openclaw::Result<void>> {
    auto response = co_await http_.get("/v1/about");
    if (!response) {
        co_return make_fail(response.error());
    }
    if (!response->is_success()) {
        co_return make_fail(
            make_error(ErrorCode::ConnectionFailed,
                       "signal-cli REST API not reachable",
                       "status=" + std::to_string(response->status)));
    }

    try {
        auto body = json::parse(response->body);
        LOG_DEBUG("[signal] signal-cli version: {}",
                  body.value("version", "unknown"));
        co_return ok_result();
    } catch (const json::exception&) {
        // Even if we can't parse the about response, the server is up
        co_return ok_result();
    }
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

auto make_signal_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>
{
    SignalConfig config;
    config.api_url = settings.value("api_url", "http://localhost:8080");
    config.phone_number = settings.value("phone_number", "");
    config.channel_name = settings.value("channel_name", "signal");
    config.poll_interval_ms = settings.value("poll_interval_ms", 1000);

    if (config.phone_number.empty()) {
        LOG_ERROR("[signal] phone_number is required in channel settings");
        return nullptr;
    }

    return std::make_unique<SignalChannel>(std::move(config), ioc);
}

} // namespace openclaw::channels
