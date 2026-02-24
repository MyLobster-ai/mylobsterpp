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

/// Configuration for the Telegram channel.
struct TelegramConfig {
    std::string bot_token;
    std::string channel_name = "telegram";
    int poll_timeout_seconds = 30;       // long-poll timeout for getUpdates
    int poll_interval_ms = 100;          // delay between poll cycles on empty
    std::optional<std::string> webhook_url;  // if set, uses webhook instead of polling
    std::optional<std::string> allowed_updates;  // JSON array of update types
    std::optional<std::string> webhook_secret;  // X-Telegram-Bot-Api-Secret-Token validation
};

/// Telegram channel implementation using the Bot API.
/// Supports long-polling via getUpdates or webhook mode.
class TelegramChannel : public Channel {
public:
    TelegramChannel(TelegramConfig config, boost::asio::io_context& ioc);
    ~TelegramChannel() override = default;

    auto start() -> boost::asio::awaitable<void> override;
    auto stop() -> boost::asio::awaitable<void> override;
    auto send(OutgoingMessage msg) -> boost::asio::awaitable<openclaw::Result<void>> override;

    [[nodiscard]] auto name() const -> std::string_view override { return config_.channel_name; }
    [[nodiscard]] auto type() const -> std::string_view override { return "telegram"; }
    [[nodiscard]] auto is_running() const noexcept -> bool override { return running_.load(); }

    /// Returns bot info fetched via getMe.
    [[nodiscard]] auto bot_username() const -> std::string_view { return bot_username_; }

private:
    /// Long-polling loop that fetches updates from Telegram.
    auto poll_loop() -> boost::asio::awaitable<void>;

    /// Processes a single Telegram update JSON object.
    auto process_update(const json& update) -> void;

    /// Parses a Telegram message object into an IncomingMessage.
    auto parse_message(const json& msg) -> IncomingMessage;

    /// Sends a text message via sendMessage.
    auto send_text(std::string_view chat_id,
                   std::string_view text,
                   std::optional<std::string_view> reply_to_message_id = std::nullopt)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Sends a document/file via sendDocument.
    auto send_document(std::string_view chat_id,
                       std::string_view url,
                       std::optional<std::string_view> caption = std::nullopt)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Sends a photo via sendPhoto.
    auto send_photo(std::string_view chat_id,
                    std::string_view url,
                    std::optional<std::string_view> caption = std::nullopt)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Sends a voice message via sendVoice (for inline audio playback).
    auto send_voice(std::string_view chat_id,
                    std::string_view url,
                    std::optional<std::string_view> caption = std::nullopt)
        -> boost::asio::awaitable<openclaw::Result<json>>;

    /// Calls the Telegram Bot API getMe to verify credentials.
    auto fetch_bot_info() -> boost::asio::awaitable<openclaw::Result<void>>;

    /// Sets bot commands via setMyCommands, capped at 100 (Telegram limit).
    auto set_bot_commands(const std::vector<std::pair<std::string, std::string>>& commands)
        -> boost::asio::awaitable<openclaw::Result<void>>;

    /// Validates a webhook request by checking the X-Telegram-Bot-Api-Secret-Token
    /// header against the configured webhook_secret. Returns true if valid or if
    /// no webhook_secret is configured. Returns false if the header is missing or
    /// mismatches.
    [[nodiscard]] auto validate_webhook_secret(std::string_view secret_header) const -> bool;

    /// Processes a webhook update JSON object. Validates the webhook secret first.
    /// Returns false if the secret validation fails.
    auto process_webhook_update(const json& update, std::string_view secret_header) -> bool;

public:
    /// Checks if an audio file is voice-compatible for sendVoice routing.
    static auto is_voice_compatible(std::string_view filename) -> bool;

    /// Builds a command list capped at 100, filtering invalid commands.
    static auto build_capped_menu_commands(
        const std::vector<std::pair<std::string, std::string>>& commands)
        -> std::vector<std::pair<std::string, std::string>>;

private:
    TelegramConfig config_;
    boost::asio::io_context& ioc_;
    infra::HttpClient http_;
    std::atomic<bool> running_{false};
    int64_t last_update_id_{0};
    std::string bot_username_;
};

/// Creates a TelegramChannel from generic ChannelConfig settings JSON.
auto make_telegram_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>;

} // namespace openclaw::channels
