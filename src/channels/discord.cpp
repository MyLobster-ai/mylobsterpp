#include "openclaw/channels/discord.hpp"
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

// Discord API constants
static constexpr std::string_view DISCORD_API_BASE = "https://discord.com/api/v10";
static constexpr std::string_view DISCORD_GATEWAY_BASE = "wss://gateway.discord.gg";

// Discord Gateway opcodes
namespace opcode {
    static constexpr int DISPATCH        = 0;
    static constexpr int HEARTBEAT       = 1;
    static constexpr int IDENTIFY        = 2;
    static constexpr int PRESENCE_UPDATE = 3;
    static constexpr int VOICE_STATE     = 4;
    static constexpr int RESUME          = 6;
    static constexpr int RECONNECT       = 7;
    static constexpr int REQUEST_MEMBERS = 8;
    static constexpr int INVALID_SESSION = 9;
    static constexpr int HELLO           = 10;
    static constexpr int HEARTBEAT_ACK   = 11;
} // namespace opcode

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

DiscordChannel::DiscordChannel(DiscordConfig config, boost::asio::io_context& ioc)
    : config_(std::move(config))
    , ioc_(ioc)
    , http_(ioc, infra::HttpClientConfig{
          .base_url = std::string(DISCORD_API_BASE),
          .timeout_seconds = 30,
          .default_headers = {
              {"Authorization", "Bot " + config_.bot_token},
              {"Content-Type", "application/json"},
              {"User-Agent", "OpenClaw (https://github.com/openclaw/openclaw, 1.0)"},
          },
      })
    , heartbeat_timer_(ioc)
{
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

auto DiscordChannel::start() -> boost::asio::awaitable<void> {
    LOG_INFO("[discord] Starting channel '{}'", config_.channel_name);
    running_.store(true);

    // Spawn the gateway loop as a detached coroutine
    boost::asio::co_spawn(ioc_, gateway_loop(), boost::asio::detached);
    co_return;
}

auto DiscordChannel::stop() -> boost::asio::awaitable<void> {
    LOG_INFO("[discord] Stopping channel '{}'", config_.channel_name);
    running_.store(false);
    heartbeat_timer_.cancel();
    co_return;
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

auto DiscordChannel::send(OutgoingMessage msg)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    if (!running_.load()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError, "Discord channel is not running"));
    }

    // Send text message
    if (!msg.text.empty()) {
        auto result = co_await send_channel_message(msg.recipient_id, msg.text,
            msg.reply_to ? std::optional<std::string_view>{*msg.reply_to} : std::nullopt);
        if (!result) {
            co_return std::unexpected(result.error());
        }
    }

    // Send attachments as separate messages with URLs
    for (const auto& attachment : msg.attachments) {
        auto result = co_await send_channel_file(msg.recipient_id, attachment.url,
            attachment.filename ? std::optional<std::string_view>{*attachment.filename}
                               : std::nullopt);
        if (!result) {
            LOG_WARN("[discord] Failed to send attachment: {}", result.error().what());
        }
    }

    co_return openclaw::Result<void>{};
}

// ---------------------------------------------------------------------------
// Gateway
// ---------------------------------------------------------------------------

auto DiscordChannel::fetch_gateway_url()
    -> boost::asio::awaitable<openclaw::Result<std::string>>
{
    auto response = co_await http_.get("/gateway/bot");
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "Failed to get gateway URL",
                       "status=" + std::to_string(response->status) + " body=" + response->body));
    }

    try {
        auto body = json::parse(response->body);
        std::string url = body.value("url", std::string(DISCORD_GATEWAY_BASE));
        url += "/?v=10&encoding=json";
        co_return url;
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError, "Failed to parse gateway response", e.what()));
    }
}

auto DiscordChannel::gateway_loop() -> boost::asio::awaitable<void> {
    LOG_DEBUG("[discord] Entering gateway loop");

    while (running_.load()) {
        try {
            // Fetch gateway URL
            auto url_result = co_await fetch_gateway_url();
            if (!url_result) {
                LOG_ERROR("[discord] Failed to fetch gateway URL: {}", url_result.error().what());
                boost::asio::steady_timer timer(ioc_, std::chrono::seconds(10));
                co_await timer.async_wait(net::use_awaitable);
                continue;
            }
            gateway_url_ = *url_result;
            LOG_INFO("[discord] Connecting to gateway: {}", gateway_url_);

            // Set up SSL context
            ssl::context ssl_ctx{ssl::context::tlsv12_client};
            ssl_ctx.set_default_verify_paths();

            // Resolve and connect
            auto resolver = net::ip::tcp::resolver(ioc_);
            auto const results = co_await resolver.async_resolve(
                "gateway.discord.gg", "443", net::use_awaitable);

            beast::ssl_stream<beast::tcp_stream> ssl_stream(ioc_, ssl_ctx);
            if (!SSL_set_tlsext_host_name(ssl_stream.native_handle(), "gateway.discord.gg")) {
                LOG_ERROR("[discord] Failed to set SNI hostname");
                continue;
            }

            auto& tcp_stream = beast::get_lowest_layer(ssl_stream);
            co_await tcp_stream.async_connect(results, net::use_awaitable);
            co_await ssl_stream.async_handshake(ssl::stream_base::client, net::use_awaitable);

            websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws(std::move(ssl_stream));
            ws.set_option(websocket::stream_base::decorator(
                [](websocket::request_type& req) {
                    req.set(beast::http::field::user_agent,
                            "OpenClaw (https://github.com/openclaw/openclaw, 1.0)");
                }));

            co_await ws.async_handshake("gateway.discord.gg",
                                         "/?v=10&encoding=json",
                                         net::use_awaitable);
            LOG_INFO("[discord] WebSocket connected to gateway");

            // Read loop
            beast::flat_buffer buffer;
            while (running_.load()) {
                buffer.clear();
                co_await ws.async_read(buffer, net::use_awaitable);
                std::string payload_str = beast::buffers_to_string(buffer.data());

                try {
                    auto payload = json::parse(payload_str);
                    int op = payload.value("op", -1);
                    auto seq = payload.contains("s") && !payload["s"].is_null()
                                   ? std::optional<int>{payload["s"].get<int>()}
                                   : std::nullopt;
                    if (seq) {
                        last_sequence_ = seq;
                    }

                    switch (op) {
                        case opcode::HELLO: {
                            int heartbeat_interval =
                                payload["d"].value("heartbeat_interval", 41250);
                            config_.heartbeat_interval_ms = heartbeat_interval;
                            LOG_DEBUG("[discord] Received HELLO, heartbeat_interval={}ms",
                                      heartbeat_interval);

                            // Send IDENTIFY
                            json identify = {
                                {"op", opcode::IDENTIFY},
                                {"d", {
                                    {"token", config_.bot_token},
                                    {"intents", config_.intents},
                                    {"properties", {
                                        {"os", "linux"},
                                        {"browser", "openclaw"},
                                        {"device", "openclaw"},
                                    }},
                                }},
                            };
                            std::string id_str = identify.dump();
                            co_await ws.async_write(
                                net::buffer(id_str), net::use_awaitable);
                            LOG_DEBUG("[discord] Sent IDENTIFY");

                            // Start heartbeat loop
                            boost::asio::co_spawn(ioc_, [this, &ws]()
                                -> boost::asio::awaitable<void> {
                                while (running_.load()) {
                                    heartbeat_timer_.expires_after(
                                        std::chrono::milliseconds(config_.heartbeat_interval_ms));
                                    try {
                                        co_await heartbeat_timer_.async_wait(net::use_awaitable);
                                    } catch (const boost::system::system_error&) {
                                        co_return;  // timer cancelled
                                    }
                                    json hb = {
                                        {"op", opcode::HEARTBEAT},
                                        {"d", last_sequence_.has_value()
                                                  ? json(*last_sequence_)
                                                  : json(nullptr)},
                                    };
                                    std::string hb_str = hb.dump();
                                    try {
                                        co_await ws.async_write(
                                            net::buffer(hb_str), net::use_awaitable);
                                        LOG_TRACE("[discord] Sent heartbeat");
                                    } catch (const std::exception& e) {
                                        LOG_WARN("[discord] Heartbeat send failed: {}", e.what());
                                        co_return;
                                    }
                                }
                            }, boost::asio::detached);
                            break;
                        }
                        case opcode::HEARTBEAT_ACK:
                            LOG_TRACE("[discord] Received heartbeat ACK");
                            break;
                        case opcode::DISPATCH:
                            handle_dispatch(payload);
                            break;
                        case opcode::RECONNECT:
                            LOG_INFO("[discord] Received RECONNECT, reconnecting...");
                            co_await ws.async_close(
                                websocket::close_code::normal, net::use_awaitable);
                            break;
                        case opcode::INVALID_SESSION: {
                            bool resumable = payload.value("d", false);
                            LOG_WARN("[discord] Invalid session, resumable={}", resumable);
                            if (!resumable) {
                                session_id_.clear();
                                last_sequence_.reset();
                            }
                            boost::asio::steady_timer wait(ioc_, std::chrono::seconds(5));
                            co_await wait.async_wait(net::use_awaitable);
                            co_await ws.async_close(
                                websocket::close_code::normal, net::use_awaitable);
                            break;
                        }
                        default:
                            LOG_TRACE("[discord] Received opcode {}", op);
                            break;
                    }
                } catch (const json::exception& e) {
                    LOG_ERROR("[discord] JSON parse error in gateway message: {}", e.what());
                }
            }

            // Clean close
            co_await ws.async_close(websocket::close_code::normal, net::use_awaitable);

        } catch (const std::exception& e) {
            LOG_ERROR("[discord] Gateway error: {}", e.what());
            heartbeat_timer_.cancel();
        }

        // Backoff before reconnecting (moved outside catch - co_await not allowed in catch blocks)
        if (running_.load()) {
            boost::asio::steady_timer timer(ioc_, std::chrono::seconds(5));
            co_await timer.async_wait(net::use_awaitable);
        }
    }

    LOG_DEBUG("[discord] Exiting gateway loop");
}

// ---------------------------------------------------------------------------
// Event handling
// ---------------------------------------------------------------------------

auto DiscordChannel::handle_dispatch(const json& payload) -> void {
    std::string event_name = payload.value("t", "");
    const auto& data = payload["d"];

    if (event_name == "READY") {
        session_id_ = data.value("session_id", "");
        if (data.contains("user")) {
            bot_user_id_ = data["user"].value("id", "");
            LOG_INFO("[discord] Ready, session={}, bot_user_id={}",
                     session_id_, bot_user_id_);
        }
    } else if (event_name == "MESSAGE_CREATE") {
        handle_message_create(data);
    } else {
        LOG_TRACE("[discord] Ignoring event: {}", event_name);
    }
}

auto DiscordChannel::handle_message_create(const json& data) -> void {
    // Ignore messages from the bot itself
    if (data.contains("author")) {
        std::string author_id = data["author"].value("id", "");
        if (author_id == bot_user_id_) {
            return;
        }
        // Ignore other bots
        if (data["author"].value("bot", false)) {
            return;
        }
    }

    auto incoming = parse_message(data);
    dispatch(std::move(incoming));
}

auto DiscordChannel::parse_message(const json& msg) -> IncomingMessage {
    IncomingMessage incoming;
    incoming.id = msg.value("id", "");
    incoming.channel = config_.channel_name;
    incoming.text = msg.value("content", "");
    incoming.received_at = Clock::now();
    incoming.raw = msg;

    // Sender info
    if (msg.contains("author")) {
        const auto& author = msg["author"];
        incoming.sender_id = author.value("id", "");
        incoming.sender_name = author.value("username", "");
        // Include discriminator if present
        std::string discriminator = author.value("discriminator", "0");
        if (discriminator != "0" && !discriminator.empty()) {
            incoming.sender_name += "#" + discriminator;
        }
    }

    // Channel ID as recipient
    incoming.raw["_channel_id"] = msg.value("channel_id", "");

    // Reply-to (message reference)
    if (msg.contains("message_reference")) {
        incoming.reply_to = msg["message_reference"].value("message_id", "");
    }

    // Thread
    if (msg.contains("thread")) {
        incoming.thread_id = msg["thread"].value("id", "");
    }

    // Attachments
    if (msg.contains("attachments") && msg["attachments"].is_array()) {
        for (const auto& att_json : msg["attachments"]) {
            Attachment att;
            att.url = att_json.value("url", "");
            att.filename = att_json.value("filename", "");
            att.size = att_json.value("size", size_t{0});

            std::string content_type = att_json.value("content_type", "");
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

    return incoming;
}

// ---------------------------------------------------------------------------
// REST API helpers
// ---------------------------------------------------------------------------

auto DiscordChannel::send_channel_message(std::string_view channel_id,
                                           std::string_view content,
                                           std::optional<std::string_view> reply_to)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    json payload = {
        {"content", std::string(content)},
    };
    if (reply_to) {
        payload["message_reference"] = json{
            {"message_id", std::string(*reply_to)},
        };
    }

    std::string path = "/channels/" + std::string(channel_id) + "/messages";
    auto response = co_await http_.post(path, payload.dump());
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "Failed to send Discord message",
                       "status=" + std::to_string(response->status) + " body=" + response->body));
    }

    try {
        co_return json::parse(response->body);
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError, "Failed to parse response", e.what()));
    }
}

auto DiscordChannel::send_channel_file(std::string_view channel_id,
                                        std::string_view url,
                                        std::optional<std::string_view> caption)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    // Discord supports URL embeds in message content
    std::string content = std::string(url);
    if (caption) {
        content = std::string(*caption) + "\n" + content;
    }
    co_return co_await send_channel_message(channel_id, content);
}

// ---------------------------------------------------------------------------
// Stubs for unused coroutine methods declared in header
// ---------------------------------------------------------------------------

auto DiscordChannel::send_identify() -> boost::asio::awaitable<void> { co_return; }
auto DiscordChannel::send_heartbeat() -> boost::asio::awaitable<void> { co_return; }
auto DiscordChannel::heartbeat_loop() -> boost::asio::awaitable<void> { co_return; }
auto DiscordChannel::send_resume() -> boost::asio::awaitable<void> { co_return; }

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

auto make_discord_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>
{
    DiscordConfig config;
    config.bot_token = settings.value("bot_token", "");
    config.channel_name = settings.value("channel_name", "discord");
    config.intents = settings.value("intents", 33281);
    if (settings.contains("application_id")) {
        config.application_id = settings.at("application_id").get<std::string>();
    }

    if (config.bot_token.empty()) {
        LOG_ERROR("[discord] bot_token is required in channel settings");
        return nullptr;
    }

    return std::make_unique<DiscordChannel>(std::move(config), ioc);
}

} // namespace openclaw::channels
