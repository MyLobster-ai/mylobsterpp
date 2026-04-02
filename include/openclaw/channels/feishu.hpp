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

/// v2026.3.7: Configuration for the Feishu (Lark) channel.
struct FeishuConfig {
    std::string app_id;
    std::string app_secret;
    std::string channel_name = "feishu";
    std::optional<std::string> verification_token;
    std::optional<std::string> encrypt_key;

    // v2026.3.7: Group broadcast
    bool group_broadcast = false;
    bool observer_session_isolation = true;

    // v2026.3.8: Markdown table rendering
    bool native_markdown_tables = true;

    // v2026.4.1: Drive comment-event flow
    bool enable_drive_comments = false;  // Dedicated Drive comment-event flow
    bool drive_comment_in_thread = true; // Reply within comment thread context

    // v2026.3.31: Streaming support
    bool enable_reasoning_stream = false; // onReasoningStream/onReasoningEnd support

    // v2026.3.31: Identity-aware card headers
    bool identity_card_headers = false;
};

/// v2026.3.7: Feishu (Lark) channel implementation.
/// Supports group broadcast, multi-app mention routing, and native markdown.
class FeishuChannel : public Channel {
public:
    FeishuChannel(FeishuConfig config, boost::asio::io_context& ioc);
    ~FeishuChannel() override = default;

    auto start() -> boost::asio::awaitable<void> override;
    auto stop() -> boost::asio::awaitable<void> override;
    auto send(OutgoingMessage msg) -> boost::asio::awaitable<openclaw::Result<void>> override;

    [[nodiscard]] auto name() const -> std::string_view override { return config_.channel_name; }
    [[nodiscard]] auto type() const -> std::string_view override { return "feishu"; }
    [[nodiscard]] auto is_running() const noexcept -> bool override { return running_.load(); }

    /// v2026.3.8: Convert markdown to Feishu card format.
    [[nodiscard]] static auto markdown_to_card(std::string_view markdown) -> json;

private:
    /// Fetches tenant access token for API authentication.
    auto fetch_tenant_token() -> boost::asio::awaitable<openclaw::Result<std::string>>;

    /// Sends a message to a Feishu chat.
    auto send_message(std::string_view chat_id, std::string_view content,
                      std::string_view msg_type = "text")
        -> boost::asio::awaitable<openclaw::Result<json>>;

    FeishuConfig config_;
    boost::asio::io_context& ioc_;
    infra::HttpClient http_;
    std::atomic<bool> running_{false};
    std::string tenant_token_;
};

/// Creates a FeishuChannel from generic ChannelConfig settings JSON.
auto make_feishu_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>;

} // namespace openclaw::channels
