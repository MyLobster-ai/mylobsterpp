#include "openclaw/channels/feishu.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::channels {

// Feishu API constants
static constexpr std::string_view FEISHU_API_BASE = "https://open.feishu.cn/open-apis";

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

FeishuChannel::FeishuChannel(FeishuConfig config, boost::asio::io_context& ioc)
    : config_(std::move(config))
    , ioc_(ioc)
    , http_(ioc, infra::HttpClientConfig{
          .base_url = std::string(FEISHU_API_BASE),
          .timeout_seconds = 30,
          .default_headers = {{"Content-Type", "application/json; charset=utf-8"}},
      })
{
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

auto FeishuChannel::start() -> boost::asio::awaitable<void> {
    LOG_INFO("[feishu] Starting channel '{}'", config_.channel_name);

    auto token_result = co_await fetch_tenant_token();
    if (!token_result) {
        LOG_ERROR("[feishu] Failed to fetch tenant token: {}", token_result.error().what());
        co_return;
    }
    tenant_token_ = *token_result;

    running_.store(true);
    LOG_INFO("[feishu] Channel '{}' started with tenant token", config_.channel_name);
}

auto FeishuChannel::stop() -> boost::asio::awaitable<void> {
    LOG_INFO("[feishu] Stopping channel '{}'", config_.channel_name);
    running_.store(false);
    co_return;
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

auto FeishuChannel::send(OutgoingMessage msg)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    if (!running_.load()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError, "Feishu channel is not running"));
    }

    if (!msg.text.empty()) {
        // v2026.3.8: Use card format for markdown tables if enabled
        if (config_.native_markdown_tables &&
            msg.text.find('|') != std::string::npos &&
            msg.text.find("---") != std::string::npos) {
            auto card = markdown_to_card(msg.text);
            auto result = co_await send_message(
                msg.recipient_id, card.dump(), "interactive");
            if (!result) {
                co_return make_fail(result.error());
            }
        } else {
            auto result = co_await send_message(msg.recipient_id, msg.text);
            if (!result) {
                co_return make_fail(result.error());
            }
        }
    }

    co_return ok_result();
}

// ---------------------------------------------------------------------------
// API helpers
// ---------------------------------------------------------------------------

auto FeishuChannel::fetch_tenant_token()
    -> boost::asio::awaitable<openclaw::Result<std::string>>
{
    json payload = {
        {"app_id", config_.app_id},
        {"app_secret", config_.app_secret},
    };

    auto response = co_await http_.post(
        "/auth/v3/tenant_access_token/internal", payload.dump());
    if (!response) {
        co_return make_fail(response.error());
    }
    if (!response->is_success()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError,
                       "Failed to fetch Feishu tenant token",
                       "status=" + std::to_string(response->status)));
    }

    try {
        auto body = json::parse(response->body);
        int code = body.value("code", -1);
        if (code != 0) {
            co_return make_fail(
                make_error(ErrorCode::ChannelError,
                           "Feishu auth failed",
                           "code=" + std::to_string(code) +
                               " msg=" + body.value("msg", "")));
        }
        co_return body.value("tenant_access_token", "");
    } catch (const json::exception& e) {
        co_return make_fail(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse Feishu auth response", e.what()));
    }
}

auto FeishuChannel::send_message(std::string_view chat_id,
                                  std::string_view content,
                                  std::string_view msg_type)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    json payload;
    if (msg_type == "text") {
        payload = {
            {"receive_id", std::string(chat_id)},
            {"msg_type", "text"},
            {"content", json{{"text", std::string(content)}}.dump()},
        };
    } else {
        // Interactive card or other types
        payload = {
            {"receive_id", std::string(chat_id)},
            {"msg_type", std::string(msg_type)},
            {"content", std::string(content)},
        };
    }

    auto response = co_await http_.post(
        "/im/v1/messages?receive_id_type=chat_id",
        payload.dump(),
        "application/json",
        {{"Authorization", "Bearer " + tenant_token_}});

    if (!response) {
        co_return make_fail(response.error());
    }
    if (!response->is_success()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError,
                       "Feishu send_message failed",
                       "status=" + std::to_string(response->status)));
    }

    try {
        co_return json::parse(response->body);
    } catch (const json::exception& e) {
        co_return make_fail(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse Feishu send response", e.what()));
    }
}

auto FeishuChannel::markdown_to_card(std::string_view markdown) -> json {
    // v2026.3.8: Convert markdown table to Feishu interactive card format.
    // This is a simplified conversion that wraps the markdown in a card element.
    json card = {
        {"config", {{"wide_screen_mode", true}}},
        {"elements", json::array({
            {
                {"tag", "div"},
                {"text", {
                    {"tag", "lark_md"},
                    {"content", std::string(markdown)},
                }},
            },
        })},
    };
    return card;
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

auto make_feishu_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>
{
    FeishuConfig config;
    config.app_id = settings.value("app_id", "");
    config.app_secret = settings.value("app_secret", "");
    config.channel_name = settings.value("channel_name", "feishu");

    if (settings.contains("verification_token")) {
        config.verification_token = settings.at("verification_token").get<std::string>();
    }
    if (settings.contains("encrypt_key")) {
        config.encrypt_key = settings.at("encrypt_key").get<std::string>();
    }

    config.group_broadcast = settings.value("group_broadcast", false);
    config.observer_session_isolation = settings.value("observer_session_isolation", true);
    config.native_markdown_tables = settings.value("native_markdown_tables", true);

    // v2026.4.1: Drive comment-event flow
    config.enable_drive_comments = settings.value("enable_drive_comments", false);
    config.drive_comment_in_thread = settings.value("drive_comment_in_thread", true);

    // v2026.3.31: Streaming and identity card headers
    config.enable_reasoning_stream = settings.value("enable_reasoning_stream", false);
    config.identity_card_headers = settings.value("identity_card_headers", false);

    if (config.app_id.empty() || config.app_secret.empty()) {
        LOG_ERROR("[feishu] app_id and app_secret are required in channel settings");
        return nullptr;
    }

    return std::make_unique<FeishuChannel>(std::move(config), ioc);
}

} // namespace openclaw::channels
