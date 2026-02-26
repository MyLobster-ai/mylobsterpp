#include "openclaw/channels/slack.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>

namespace openclaw::channels {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;

// Slack API constants
static constexpr std::string_view SLACK_API_BASE = "https://slack.com/api";

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SlackChannel::SlackChannel(SlackConfig config, boost::asio::io_context& ioc)
    : config_(std::move(config))
    , ioc_(ioc)
    , http_(ioc, infra::HttpClientConfig{
          .base_url = std::string(SLACK_API_BASE),
          .timeout_seconds = 30,
          .default_headers = {
              {"Authorization", "Bearer " + config_.bot_token},
              {"Content-Type", "application/json; charset=utf-8"},
          },
      })
{
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

auto SlackChannel::start() -> boost::asio::awaitable<void> {
    LOG_INFO("[slack] Starting channel '{}'", config_.channel_name);

    auto info = co_await fetch_bot_info();
    if (!info) {
        LOG_ERROR("[slack] Failed to fetch bot info: {}", info.error().what());
        co_return;
    }
    LOG_INFO("[slack] Bot authenticated as {} (user_id={}, team={})",
             bot_name_, bot_user_id_, team_id_);

    running_.store(true);

    if (config_.use_socket_mode) {
        // Open Socket Mode connection
        boost::asio::co_spawn(ioc_, socket_mode_loop(), boost::asio::detached);
    } else {
        LOG_INFO("[slack] Events API mode - waiting for webhook events");
        // In Events API mode, an external HTTP server must call handle_socket_event
    }
}

auto SlackChannel::stop() -> boost::asio::awaitable<void> {
    LOG_INFO("[slack] Stopping channel '{}'", config_.channel_name);
    running_.store(false);
    co_return;
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

auto SlackChannel::send(OutgoingMessage msg)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    if (!running_.load()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError, "Slack channel is not running"));
    }

    // Thread-ownership outbound gating:
    // If sending to a thread we didn't start, check for @-mention bypass
    // or allow the hooks layer (DeliveredChannel) to cancel/modify.
    // Skip gating if text contains @BOT_USER_ID mention
    bool has_mention = !bot_user_id_.empty() &&
        msg.text.find("<@" + bot_user_id_ + ">") != std::string::npos;
    if (!has_mention && msg.thread_id) {
        LOG_TRACE("[slack] Outbound to thread {} (gating deferred to hooks layer)",
                  *msg.thread_id);
    }

    // Determine thread_ts based on reply_to_mode config
    std::optional<std::string> effective_thread_ts;
    if (msg.thread_id) {
        if (!config_.reply_to_mode || *config_.reply_to_mode == "auto" || *config_.reply_to_mode == "thread") {
            effective_thread_ts = *msg.thread_id;
        }
        // "channel" mode: don't set thread_ts (reply goes to channel)
    }

    // Send text message
    if (!msg.text.empty()) {
        auto result = co_await post_message(msg.recipient_id, msg.text,
            effective_thread_ts ? std::optional<std::string_view>{*effective_thread_ts} : std::nullopt);
        if (!result) {
            co_return std::unexpected(result.error());
        }

        // Track threads we've posted to so we can avoid re-entry
        if (effective_thread_ts) {
            thread_sessions_[*effective_thread_ts] = true;
        }
    }

    // Send attachments
    for (const auto& attachment : msg.attachments) {
        auto result = co_await upload_file(msg.recipient_id, attachment.url,
            attachment.filename ? std::optional<std::string_view>{*attachment.filename}
                               : std::nullopt);
        if (!result) {
            LOG_WARN("[slack] Failed to upload file: {}", result.error().what());
        }
    }

    co_return openclaw::Result<void>{};
}

// ---------------------------------------------------------------------------
// Socket Mode
// ---------------------------------------------------------------------------

auto SlackChannel::open_socket_mode()
    -> boost::asio::awaitable<openclaw::Result<std::string>>
{
    // Use app-level token for Socket Mode
    auto response = co_await http_.post("/apps.connections.open", "",
        "application/x-www-form-urlencoded",
        {{"Authorization", "Bearer " + config_.app_token}});

    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "Failed to open Socket Mode connection",
                       "status=" + std::to_string(response->status) + " body=" + response->body));
    }

    try {
        auto body = json::parse(response->body);
        if (!body.value("ok", false)) {
            co_return std::unexpected(
                make_error(ErrorCode::ChannelError,
                           "apps.connections.open returned ok=false",
                           body.value("error", "unknown")));
        }
        co_return body.value("url", "");
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse Socket Mode response", e.what()));
    }
}

auto SlackChannel::socket_mode_loop() -> boost::asio::awaitable<void> {
    LOG_DEBUG("[slack] Entering Socket Mode loop");

    while (running_.load()) {
        try {
            // Get a fresh Socket Mode URL
            auto url_result = co_await open_socket_mode();
            if (!url_result) {
                LOG_ERROR("[slack] Failed to get Socket Mode URL: {}",
                          url_result.error().what());
                boost::asio::steady_timer timer(ioc_, std::chrono::seconds(10));
                co_await timer.async_wait(net::use_awaitable);
                continue;
            }
            socket_url_ = *url_result;
            LOG_INFO("[slack] Connecting to Socket Mode: {}", socket_url_);

            // Parse the WSS URL (remove wss:// prefix to get host and path)
            std::string host;
            std::string path = "/";
            {
                auto url = socket_url_;
                if (url.starts_with("wss://")) url = url.substr(6);
                auto slash_pos = url.find('/');
                if (slash_pos != std::string::npos) {
                    host = url.substr(0, slash_pos);
                    path = url.substr(slash_pos);
                } else {
                    host = url;
                }
            }

            // SSL + WebSocket setup
            ssl::context ssl_ctx{ssl::context::tlsv12_client};
            ssl_ctx.set_default_verify_paths();

            auto resolver = net::ip::tcp::resolver(ioc_);
            auto const results = co_await resolver.async_resolve(
                host, "443", net::use_awaitable);

            beast::ssl_stream<beast::tcp_stream> ssl_stream(ioc_, ssl_ctx);
            if (!SSL_set_tlsext_host_name(ssl_stream.native_handle(), host.c_str())) {
                LOG_ERROR("[slack] Failed to set SNI hostname");
                continue;
            }

            auto& tcp_stream = beast::get_lowest_layer(ssl_stream);
            co_await tcp_stream.async_connect(results, net::use_awaitable);
            co_await ssl_stream.async_handshake(ssl::stream_base::client, net::use_awaitable);

            websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws(std::move(ssl_stream));
            co_await ws.async_handshake(host, path, net::use_awaitable);
            LOG_INFO("[slack] Socket Mode WebSocket connected");

            // Read loop
            beast::flat_buffer buffer;
            while (running_.load()) {
                buffer.clear();
                co_await ws.async_read(buffer, net::use_awaitable);
                std::string msg_str = beast::buffers_to_string(buffer.data());

                try {
                    auto envelope = json::parse(msg_str);
                    std::string envelope_type = envelope.value("type", "");

                    if (envelope_type == "hello") {
                        LOG_DEBUG("[slack] Received Socket Mode hello");
                        continue;
                    }

                    // Acknowledge the envelope immediately
                    if (envelope.contains("envelope_id")) {
                        std::string envelope_id = envelope["envelope_id"];
                        json ack = {{"envelope_id", envelope_id}};
                        std::string ack_str = ack.dump();
                        co_await ws.async_write(
                            net::buffer(ack_str), net::use_awaitable);
                        LOG_TRACE("[slack] Acknowledged envelope {}", envelope_id);
                    }

                    if (envelope_type == "events_api") {
                        handle_socket_event(envelope);
                    } else if (envelope_type == "disconnect") {
                        LOG_INFO("[slack] Received disconnect, reconnecting...");
                        break;
                    } else if (envelope_type == "slash_commands") {
                        LOG_DEBUG("[slack] Received slash command (not handled)");
                    } else if (envelope_type == "interactive") {
                        LOG_DEBUG("[slack] Received interactive event (not handled)");
                    }
                } catch (const json::exception& e) {
                    LOG_ERROR("[slack] JSON parse error: {}", e.what());
                }
            }

            co_await ws.async_close(websocket::close_code::normal, net::use_awaitable);

        } catch (const std::exception& e) {
            LOG_ERROR("[slack] Socket Mode error: {}", e.what());
        }

        // Backoff before reconnecting (moved outside catch - co_await not allowed in catch blocks)
        if (running_.load()) {
            boost::asio::steady_timer timer(ioc_, std::chrono::seconds(5));
            co_await timer.async_wait(net::use_awaitable);
        }
    }

    LOG_DEBUG("[slack] Exiting Socket Mode loop");
}

// ---------------------------------------------------------------------------
// Event handling
// ---------------------------------------------------------------------------

auto SlackChannel::handle_socket_event(const json& envelope) -> void {
    if (!envelope.contains("payload")) {
        return;
    }

    const auto& payload = envelope["payload"];
    std::string event_type = payload.value("type", "");

    if (event_type == "event_callback") {
        if (payload.contains("event")) {
            const auto& event = payload["event"];
            std::string type = event.value("type", "");

            if (type == "message" || type == "app_mention") {
                handle_message_event(event);
            }
        }
    }
}

auto SlackChannel::authorize_system_event_sender(
    std::string_view sender_id, std::string_view channel_id,
    std::string_view event_type) const -> bool
{
    if (!is_channel_allowed(channel_id)) {
        LOG_DEBUG("[slack] System event '{}' in channel {} blocked by channel_allowlist",
                  event_type, channel_id);
        return false;
    }
    return true;
}

auto SlackChannel::is_channel_allowed(std::string_view channel_id) const -> bool {
    if (config_.channel_allowlist.empty()) {
        return true;
    }

    for (const auto& allowed : config_.channel_allowlist) {
        if (config_.case_insensitive_allowlist) {
            // Case-insensitive comparison
            if (allowed.size() == channel_id.size()) {
                bool match = true;
                for (size_t i = 0; i < allowed.size(); ++i) {
                    if (std::tolower(static_cast<unsigned char>(allowed[i])) !=
                        std::tolower(static_cast<unsigned char>(channel_id[i]))) {
                        match = false;
                        break;
                    }
                }
                if (match) return true;
            }
        } else {
            if (allowed == channel_id) return true;
        }
    }
    return false;
}

auto SlackChannel::handle_message_event(const json& event) -> void {
    // Ignore bot messages
    if (event.contains("bot_id") || event.value("subtype", "") == "bot_message") {
        return;
    }
    // Ignore the bot's own messages
    std::string user = event.value("user", "");
    if (user == bot_user_id_) {
        return;
    }
    // Ignore message subtypes (edits, deletions, etc.) except thread broadcasts
    std::string subtype = event.value("subtype", "");
    if (!subtype.empty() && subtype != "thread_broadcast") {
        return;
    }

    // Skip messages in threads we initiated (to avoid re-entry / self-reply loops)
    // unless the message contains an @-mention of the bot
    if (event.contains("thread_ts")) {
        std::string thread_ts = event["thread_ts"].get<std::string>();
        if (thread_sessions_.contains(thread_ts)) {
            // Check if the message @-mentions the bot (bypass re-entry guard)
            std::string text = event.value("text", "");
            bool has_mention = !bot_user_id_.empty() &&
                text.find("<@" + bot_user_id_ + ">") != std::string::npos;
            if (!has_mention) {
                LOG_TRACE("[slack] Skipping thread message in owned thread {}",
                          thread_ts);
                return;
            }
        }
    }

    auto incoming = parse_message(event);
    dispatch(std::move(incoming));
}

auto SlackChannel::parse_message(const json& event) -> IncomingMessage {
    IncomingMessage incoming;
    incoming.channel = config_.channel_name;
    incoming.received_at = Clock::now();
    incoming.raw = event;

    // Message ID: use client_msg_id if available, else ts
    incoming.id = event.value("client_msg_id", event.value("ts", utils::generate_id()));

    // Sender
    incoming.sender_id = event.value("user", "");
    incoming.sender_name = incoming.sender_id;  // Slack requires a separate API call for name

    // Text
    incoming.text = event.value("text", "");

    // Thread
    if (event.contains("thread_ts")) {
        incoming.thread_id = event["thread_ts"].get<std::string>();
    }

    // Channel ID stored in raw for sending replies
    std::string channel_id = event.value("channel", "");
    incoming.raw["_channel_id"] = channel_id;

    // v2026.2.24: Treat D* channel IDs as DMs regardless of channel_type field
    bool is_dm = channel_id.starts_with("D");
    if (!is_dm && event.contains("channel_type")) {
        is_dm = event["channel_type"].get<std::string>() == "im";
    }
    incoming.raw["_is_dm"] = is_dm;

    // Attachments (files)
    if (event.contains("files") && event["files"].is_array()) {
        for (const auto& file : event["files"]) {
            Attachment att;
            att.url = file.value("url_private", "");
            att.filename = file.value("name", "");
            att.size = file.value("size", size_t{0});

            // Enforce media download byte limit
            if (att.size && *att.size > kMaxMediaDownloadBytes) {
                LOG_WARN("[slack] Skipping oversized attachment '{}' ({} bytes)",
                         att.filename.value_or(""), *att.size);
                continue;
            }

            std::string mimetype = file.value("mimetype", "");
            if (mimetype.starts_with("image/")) {
                att.type = "image";
            } else if (mimetype.starts_with("video/")) {
                att.type = "video";
            } else if (mimetype.starts_with("audio/")) {
                att.type = "audio";
            } else {
                att.type = "file";
            }

            incoming.attachments.push_back(std::move(att));
        }
    }

    return incoming;
}

auto SlackChannel::send_socket_ack(std::string_view envelope_id) -> void {
    // This is handled inline in the socket_mode_loop
    LOG_TRACE("[slack] Ack for envelope {} (handled inline)", envelope_id);
}

// ---------------------------------------------------------------------------
// Web API helpers
// ---------------------------------------------------------------------------

auto SlackChannel::post_message(std::string_view channel_id,
                                 std::string_view text,
                                 std::optional<std::string_view> thread_ts)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    json payload = {
        {"channel", std::string(channel_id)},
        {"text", std::string(text)},
    };
    if (thread_ts) {
        payload["thread_ts"] = std::string(*thread_ts);
    }

    auto response = co_await http_.post("/chat.postMessage", payload.dump());
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "chat.postMessage failed",
                       "status=" + std::to_string(response->status) + " body=" + response->body));
    }

    try {
        auto body = json::parse(response->body);
        if (!body.value("ok", false)) {
            co_return std::unexpected(
                make_error(ErrorCode::ChannelError,
                           "chat.postMessage returned ok=false",
                           body.value("error", "unknown")));
        }
        co_return body;
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse postMessage response", e.what()));
    }
}

auto SlackChannel::upload_file(std::string_view channel_id,
                                std::string_view url,
                                std::optional<std::string_view> filename,
                                std::optional<std::string_view> title)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    // Slack files.uploadV2 workflow:
    // Step 1: Get an upload URL
    std::string fname = filename ? std::string(*filename) : "file";
    json get_url_payload = {
        {"filename", fname},
        {"length", 0},  // Placeholder - real implementation would fetch the file first
    };

    auto url_response = co_await http_.post("/files.getUploadURLExternal",
                                             get_url_payload.dump());
    if (!url_response) {
        co_return std::unexpected(url_response.error());
    }

    try {
        auto url_body = json::parse(url_response->body);
        if (!url_body.value("ok", false)) {
            co_return std::unexpected(
                make_error(ErrorCode::ChannelError,
                           "files.getUploadURLExternal failed",
                           url_body.value("error", "unknown")));
        }

        std::string upload_url = url_body.value("upload_url", "");
        std::string file_id = url_body.value("file_id", "");

        // Step 2: Complete the upload (share to channel)
        json complete_payload = {
            {"files", json::array({{{"id", file_id}, {"title", title ? std::string(*title) : fname}}})},
            {"channel_id", std::string(channel_id)},
        };

        auto complete_response = co_await http_.post("/files.completeUploadExternal",
                                                      complete_payload.dump());
        if (!complete_response) {
            co_return std::unexpected(complete_response.error());
        }

        auto complete_body = json::parse(complete_response->body);
        if (!complete_body.value("ok", false)) {
            co_return std::unexpected(
                make_error(ErrorCode::ChannelError,
                           "files.completeUploadExternal failed",
                           complete_body.value("error", "unknown")));
        }

        co_return complete_body;
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse file upload response", e.what()));
    }
}

auto SlackChannel::fetch_bot_info() -> boost::asio::awaitable<openclaw::Result<void>> {
    auto response = co_await http_.post("/auth.test", "");
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "auth.test failed",
                       "status=" + std::to_string(response->status)));
    }

    try {
        auto body = json::parse(response->body);
        if (!body.value("ok", false)) {
            co_return std::unexpected(
                make_error(ErrorCode::Unauthorized,
                           "auth.test returned ok=false",
                           body.value("error", "unknown")));
        }
        bot_user_id_ = body.value("user_id", "");
        bot_name_ = body.value("user", "");
        team_id_ = body.value("team_id", "");
        co_return openclaw::Result<void>{};
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse auth.test response", e.what()));
    }
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

auto make_slack_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>
{
    SlackConfig config;
    config.bot_token = settings.value("bot_token", "");
    config.app_token = settings.value("app_token", "");
    config.channel_name = settings.value("channel_name", "slack");
    config.use_socket_mode = settings.value("use_socket_mode", true);
    if (settings.contains("signing_secret")) {
        config.signing_secret = settings.at("signing_secret").get<std::string>();
    }
    if (settings.contains("reply_to_mode")) {
        config.reply_to_mode = settings.at("reply_to_mode").get<std::string>();
    }

    if (config.bot_token.empty()) {
        LOG_ERROR("[slack] bot_token is required in channel settings");
        return nullptr;
    }
    if (config.use_socket_mode && config.app_token.empty()) {
        LOG_ERROR("[slack] app_token is required for Socket Mode");
        return nullptr;
    }

    return std::make_unique<SlackChannel>(std::move(config), ioc);
}

} // namespace openclaw::channels
