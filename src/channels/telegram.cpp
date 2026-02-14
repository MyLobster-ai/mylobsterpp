#include "openclaw/channels/telegram.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <algorithm>
#include <regex>
#include <set>
#include <boost/asio/steady_timer.hpp>

namespace openclaw::channels {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TelegramChannel::TelegramChannel(TelegramConfig config, boost::asio::io_context& ioc)
    : config_(std::move(config))
    , ioc_(ioc)
    , http_(ioc, infra::HttpClientConfig{
          .base_url = "https://api.telegram.org/bot" + config_.bot_token,
          .timeout_seconds = config_.poll_timeout_seconds + 10,
          .default_headers = {{"Content-Type", "application/json"}},
      })
{
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

auto TelegramChannel::start() -> boost::asio::awaitable<void> {
    LOG_INFO("[telegram] Starting channel '{}'", config_.channel_name);

    auto result = co_await fetch_bot_info();
    if (!result) {
        LOG_ERROR("[telegram] Failed to fetch bot info: {}", result.error().what());
        co_return;
    }

    LOG_INFO("[telegram] Bot authenticated as @{}", bot_username_);
    running_.store(true);

    // Spawn the polling loop as a detached coroutine
    boost::asio::co_spawn(ioc_, poll_loop(), boost::asio::detached);
}

auto TelegramChannel::stop() -> boost::asio::awaitable<void> {
    LOG_INFO("[telegram] Stopping channel '{}'", config_.channel_name);
    running_.store(false);
    co_return;
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

auto TelegramChannel::send(OutgoingMessage msg)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    if (!running_.load()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError, "Telegram channel is not running"));
    }

    // Send text first
    if (!msg.text.empty()) {
        auto result = co_await send_text(msg.recipient_id, msg.text,
            msg.reply_to ? std::optional<std::string_view>{*msg.reply_to} : std::nullopt);
        if (!result) {
            co_return std::unexpected(result.error());
        }
    }

    // Send each attachment with voice-compatible routing
    for (const auto& attachment : msg.attachments) {
        if (attachment.type == "image") {
            auto result = co_await send_photo(msg.recipient_id, attachment.url,
                attachment.filename ? std::optional<std::string_view>{*attachment.filename}
                                   : std::nullopt);
            if (!result) {
                LOG_WARN("[telegram] Failed to send photo: {}", result.error().what());
            }
        } else if (attachment.type == "audio" && is_voice_compatible(
                attachment.filename ? *attachment.filename : "")) {
            // Route MP3/M4A/OGG audio through sendVoice for inline playback
            auto result = co_await send_voice(msg.recipient_id, attachment.url,
                attachment.filename ? std::optional<std::string_view>{*attachment.filename}
                                   : std::nullopt);
            if (!result) {
                LOG_WARN("[telegram] Failed to send voice: {}", result.error().what());
            }
        } else {
            auto result = co_await send_document(msg.recipient_id, attachment.url,
                attachment.filename ? std::optional<std::string_view>{*attachment.filename}
                                   : std::nullopt);
            if (!result) {
                LOG_WARN("[telegram] Failed to send document: {}", result.error().what());
            }
        }
    }

    co_return openclaw::Result<void>{};
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

auto TelegramChannel::poll_loop() -> boost::asio::awaitable<void> {
    LOG_DEBUG("[telegram] Entering poll loop");

    while (running_.load()) {
        json params = {
            {"timeout", config_.poll_timeout_seconds},
            {"allowed_updates", json::array({"message", "edited_message", "callback_query"})},
        };
        if (last_update_id_ > 0) {
            params["offset"] = last_update_id_ + 1;
        }

        auto response = co_await http_.post("/getUpdates", params.dump());
        if (!response) {
            LOG_WARN("[telegram] getUpdates failed: {}", response.error().what());
            // Back off before retrying
            boost::asio::steady_timer timer(ioc_, std::chrono::seconds(5));
            co_await timer.async_wait(boost::asio::use_awaitable);
            continue;
        }

        if (!response->is_success()) {
            LOG_WARN("[telegram] getUpdates returned status {}: {}",
                     response->status, response->body);
            boost::asio::steady_timer timer(ioc_, std::chrono::seconds(5));
            co_await timer.async_wait(boost::asio::use_awaitable);
            continue;
        }

        try {
            auto body = json::parse(response->body);
            if (!body.value("ok", false)) {
                LOG_WARN("[telegram] API returned ok=false: {}", body.dump());
                continue;
            }

            const auto& results = body["result"];
            for (const auto& update : results) {
                process_update(update);
            }
        } catch (const json::exception& e) {
            LOG_ERROR("[telegram] JSON parse error: {}", e.what());
        }

        // Small delay between poll cycles if no updates
        if (config_.poll_interval_ms > 0) {
            boost::asio::steady_timer timer(
                ioc_, std::chrono::milliseconds(config_.poll_interval_ms));
            co_await timer.async_wait(boost::asio::use_awaitable);
        }
    }

    LOG_DEBUG("[telegram] Exiting poll loop");
}

// ---------------------------------------------------------------------------
// Update processing
// ---------------------------------------------------------------------------

auto TelegramChannel::process_update(const json& update) -> void {
    int64_t update_id = update.value("update_id", int64_t{0});
    if (update_id > last_update_id_) {
        last_update_id_ = update_id;
    }

    if (update.contains("message")) {
        auto incoming = parse_message(update["message"]);
        dispatch(std::move(incoming));
    } else if (update.contains("edited_message")) {
        auto incoming = parse_message(update["edited_message"]);
        dispatch(std::move(incoming));
    } else if (update.contains("callback_query")) {
        const auto& cb = update["callback_query"];
        if (cb.contains("message")) {
            auto incoming = parse_message(cb["message"]);
            incoming.text = cb.value("data", "");
            incoming.raw = update;
            dispatch(std::move(incoming));
        }
    }
}

auto TelegramChannel::parse_message(const json& msg) -> IncomingMessage {
    IncomingMessage incoming;
    incoming.id = std::to_string(msg.value("message_id", 0));
    incoming.channel = config_.channel_name;
    incoming.received_at = Clock::now();
    incoming.raw = msg;

    // Sender info
    if (msg.contains("from")) {
        const auto& from = msg["from"];
        incoming.sender_id = std::to_string(from.value("id", int64_t{0}));
        std::string first = from.value("first_name", "");
        std::string last = from.value("last_name", "");
        incoming.sender_name = first;
        if (!last.empty()) {
            incoming.sender_name += " " + last;
        }
    }

    // Chat ID as a fallback sender_id for group messages
    if (msg.contains("chat")) {
        const auto& chat = msg["chat"];
        std::string chat_id = std::to_string(chat.value("id", int64_t{0}));
        // For groups, sender_id is the user, but we track the chat as recipient_id
        incoming.raw["_chat_id"] = chat_id;
    }

    // Text
    incoming.text = msg.value("text", "");
    if (incoming.text.empty()) {
        incoming.text = msg.value("caption", "");
    }

    // Reply-to
    if (msg.contains("reply_to_message")) {
        incoming.reply_to = std::to_string(
            msg["reply_to_message"].value("message_id", 0));
    }

    // Thread (forum topic)
    if (msg.contains("message_thread_id")) {
        incoming.thread_id = std::to_string(msg.value("message_thread_id", 0));
    }

    // Attachments
    if (msg.contains("photo") && msg["photo"].is_array() && !msg["photo"].empty()) {
        // Take the largest photo (last in array)
        const auto& photo = msg["photo"].back();
        Attachment att;
        att.type = "image";
        att.url = photo.value("file_id", "");  // file_id, needs getFile to get URL
        if (photo.contains("file_size")) {
            att.size = photo.value("file_size", size_t{0});
        }
        incoming.attachments.push_back(std::move(att));
    }
    if (msg.contains("document")) {
        const auto& doc = msg["document"];
        Attachment att;
        att.type = "file";
        att.url = doc.value("file_id", "");
        att.filename = doc.value("file_name", "");
        if (doc.contains("file_size")) {
            att.size = doc.value("file_size", size_t{0});
        }
        incoming.attachments.push_back(std::move(att));
    }
    if (msg.contains("audio")) {
        const auto& audio = msg["audio"];
        Attachment att;
        att.type = "audio";
        att.url = audio.value("file_id", "");
        att.filename = audio.value("file_name", "");
        if (audio.contains("file_size")) {
            att.size = audio.value("file_size", size_t{0});
        }
        incoming.attachments.push_back(std::move(att));
    }
    if (msg.contains("video")) {
        const auto& video = msg["video"];
        Attachment att;
        att.type = "video";
        att.url = video.value("file_id", "");
        att.filename = video.value("file_name", "");
        if (video.contains("file_size")) {
            att.size = video.value("file_size", size_t{0});
        }
        incoming.attachments.push_back(std::move(att));
    }
    if (msg.contains("voice")) {
        const auto& voice = msg["voice"];
        Attachment att;
        att.type = "audio";
        att.url = voice.value("file_id", "");
        if (voice.contains("file_size")) {
            att.size = voice.value("file_size", size_t{0});
        }
        incoming.attachments.push_back(std::move(att));
    }

    return incoming;
}

// ---------------------------------------------------------------------------
// API helpers
// ---------------------------------------------------------------------------

auto TelegramChannel::send_text(std::string_view chat_id,
                                 std::string_view text,
                                 std::optional<std::string_view> reply_to_message_id)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    json payload = {
        {"chat_id", std::string(chat_id)},
        {"text", std::string(text)},
        {"parse_mode", "Markdown"},
    };
    if (reply_to_message_id) {
        payload["reply_parameters"] = json{
            {"message_id", std::stoll(std::string(*reply_to_message_id))},
        };
    }

    auto response = co_await http_.post("/sendMessage", payload.dump());
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "sendMessage failed",
                       "status=" + std::to_string(response->status) + " body=" + response->body));
    }

    try {
        auto body = json::parse(response->body);
        co_return body;
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError, "Failed to parse sendMessage response", e.what()));
    }
}

auto TelegramChannel::send_document(std::string_view chat_id,
                                     std::string_view url,
                                     std::optional<std::string_view> caption)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    json payload = {
        {"chat_id", std::string(chat_id)},
        {"document", std::string(url)},
    };
    if (caption) {
        payload["caption"] = std::string(*caption);
    }

    auto response = co_await http_.post("/sendDocument", payload.dump());
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "sendDocument failed",
                       "status=" + std::to_string(response->status)));
    }

    try {
        co_return json::parse(response->body);
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError, "Failed to parse sendDocument response", e.what()));
    }
}

auto TelegramChannel::send_photo(std::string_view chat_id,
                                  std::string_view url,
                                  std::optional<std::string_view> caption)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    json payload = {
        {"chat_id", std::string(chat_id)},
        {"photo", std::string(url)},
    };
    if (caption) {
        payload["caption"] = std::string(*caption);
    }

    auto response = co_await http_.post("/sendPhoto", payload.dump());
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "sendPhoto failed",
                       "status=" + std::to_string(response->status)));
    }

    try {
        co_return json::parse(response->body);
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError, "Failed to parse sendPhoto response", e.what()));
    }
}

auto TelegramChannel::fetch_bot_info() -> boost::asio::awaitable<openclaw::Result<void>> {
    auto response = co_await http_.get("/getMe");
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "getMe failed",
                       "status=" + std::to_string(response->status) + " body=" + response->body));
    }

    try {
        auto body = json::parse(response->body);
        if (!body.value("ok", false)) {
            co_return std::unexpected(
                make_error(ErrorCode::ChannelError, "getMe returned ok=false"));
        }
        bot_username_ = body["result"].value("username", "unknown");
        co_return openclaw::Result<void>{};
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError, "Failed to parse getMe response", e.what()));
    }
}

// ---------------------------------------------------------------------------
// Voice routing
// ---------------------------------------------------------------------------

auto TelegramChannel::is_voice_compatible(std::string_view filename) -> bool {
    // Check if the file is an audio format Telegram can play inline as voice
    std::string lower(filename);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return lower.ends_with(".mp3") ||
           lower.ends_with(".m4a") ||
           lower.ends_with(".ogg") ||
           lower.ends_with(".oga") ||
           lower.ends_with(".opus");
}

auto TelegramChannel::send_voice(std::string_view chat_id,
                                  std::string_view url,
                                  std::optional<std::string_view> caption)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    json payload = {
        {"chat_id", std::string(chat_id)},
        {"voice", std::string(url)},
    };
    if (caption) {
        payload["caption"] = std::string(*caption);
    }

    auto response = co_await http_.post("/sendVoice", payload.dump());
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "sendVoice failed",
                       "status=" + std::to_string(response->status)));
    }

    try {
        co_return json::parse(response->body);
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError, "Failed to parse sendVoice response", e.what()));
    }
}

// ---------------------------------------------------------------------------
// Bot command menu (100-command cap)
// ---------------------------------------------------------------------------

auto TelegramChannel::build_capped_menu_commands(
    const std::vector<std::pair<std::string, std::string>>& commands)
    -> std::vector<std::pair<std::string, std::string>>
{
    static constexpr size_t kMaxCommands = 100;
    static const std::regex kValidCommand("^[a-z0-9_]{1,32}$");

    std::vector<std::pair<std::string, std::string>> valid;
    std::set<std::string> seen;

    for (const auto& [cmd, desc] : commands) {
        // Validate command format
        if (!std::regex_match(cmd, kValidCommand)) {
            LOG_WARN("[telegram] Skipping invalid command '{}' (must match [a-z0-9_]{{1,32}})", cmd);
            continue;
        }

        // Deduplicate
        if (seen.contains(cmd)) {
            continue;
        }
        seen.insert(cmd);

        valid.emplace_back(cmd, desc);

        if (valid.size() >= kMaxCommands) {
            break;
        }
    }

    if (commands.size() > kMaxCommands) {
        LOG_WARN("[telegram] Registered {} of {} commands (Telegram limit: {})",
                 valid.size(), commands.size(), kMaxCommands);
    }

    return valid;
}

auto TelegramChannel::set_bot_commands(
    const std::vector<std::pair<std::string, std::string>>& commands)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    auto capped = build_capped_menu_commands(commands);

    json cmd_array = json::array();
    for (const auto& [cmd, desc] : capped) {
        cmd_array.push_back({
            {"command", cmd},
            {"description", desc},
        });
    }

    json payload = {{"commands", cmd_array}};
    auto response = co_await http_.post("/setMyCommands", payload.dump());
    if (!response) {
        co_return std::unexpected(response.error());
    }
    if (!response->is_success()) {
        co_return std::unexpected(
            make_error(ErrorCode::ChannelError,
                       "setMyCommands failed",
                       "status=" + std::to_string(response->status)));
    }

    LOG_INFO("[telegram] Set {} bot commands", capped.size());
    co_return openclaw::Result<void>{};
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

auto make_telegram_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>
{
    TelegramConfig config;
    config.bot_token = settings.value("bot_token", "");
    config.channel_name = settings.value("channel_name", "telegram");
    config.poll_timeout_seconds = settings.value("poll_timeout_seconds", 30);
    config.poll_interval_ms = settings.value("poll_interval_ms", 100);
    if (settings.contains("webhook_url")) {
        config.webhook_url = settings.at("webhook_url").get<std::string>();
    }

    if (config.bot_token.empty()) {
        LOG_ERROR("[telegram] bot_token is required in channel settings");
        return nullptr;
    }

    return std::make_unique<TelegramChannel>(std::move(config), ioc);
}

} // namespace openclaw::channels
