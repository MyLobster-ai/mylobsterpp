#include "openclaw/channels/matrix.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <boost/asio/steady_timer.hpp>

namespace openclaw::channels {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MatrixChannel::MatrixChannel(MatrixConfig config, boost::asio::io_context& ioc)
    : config_(std::move(config))
    , ioc_(ioc)
    , http_(ioc, infra::HttpClientConfig{
          .base_url = config_.homeserver_url,
          .timeout_seconds = 60,
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

auto MatrixChannel::start() -> boost::asio::awaitable<void> {
    LOG_INFO("[matrix] Starting channel '{}'", config_.channel_name);
    running_.store(true);

    // Spawn sync loop
    boost::asio::co_spawn(ioc_, sync_loop(), boost::asio::detached);
}

auto MatrixChannel::stop() -> boost::asio::awaitable<void> {
    LOG_INFO("[matrix] Stopping channel '{}'", config_.channel_name);
    running_.store(false);
    co_return;
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

auto MatrixChannel::send(OutgoingMessage msg)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    if (!running_.load()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError, "Matrix channel is not running"));
    }

    if (!msg.text.empty()) {
        auto txn_id = utils::generate_id(16);
        std::string path = "/_matrix/client/v3/rooms/" +
                           std::string(msg.recipient_id) +
                           "/send/m.room.message/" + txn_id;

        json payload = {
            {"msgtype", "m.text"},
            {"body", msg.text},
        };

        auto response = co_await http_.put(path, payload.dump());
        if (!response) {
            co_return make_fail(response.error());
        }
        if (!response->is_success()) {
            co_return make_fail(
                make_error(ErrorCode::ChannelError,
                           "Matrix send failed",
                           "status=" + std::to_string(response->status)));
        }
    }

    co_return ok_result();
}

// ---------------------------------------------------------------------------
// Room agent resolution (v2026.3.7)
// ---------------------------------------------------------------------------

auto MatrixChannel::resolve_room_agent(std::string_view room_id) const
    -> std::optional<std::string>
{
    if (config_.room_bindings.is_null() || !config_.room_bindings.is_object()) {
        return std::nullopt;
    }

    auto it = config_.room_bindings.find(std::string(room_id));
    if (it == config_.room_bindings.end()) {
        return std::nullopt;
    }

    if (it->is_string()) {
        return it->get<std::string>();
    }
    if (it->is_object() && it->contains("agentId")) {
        return (*it)["agentId"].get<std::string>();
    }

    return std::nullopt;
}

auto MatrixChannel::is_dm_room(std::string_view room_id) const -> bool {
    // v2026.3.7: If room has an explicit binding, it is NOT a DM
    // (explicit bindings override m.direct classification)
    if (resolve_room_agent(room_id).has_value()) {
        return false;
    }

    // Fallback: check room membership count or m.direct account data
    // In a real implementation this would query the homeserver.
    // For now, return false as a safe default.
    return false;
}

// ---------------------------------------------------------------------------
// Sync loop
// ---------------------------------------------------------------------------

auto MatrixChannel::sync_loop() -> boost::asio::awaitable<void> {
    LOG_DEBUG("[matrix] Entering sync loop");
    namespace net = boost::asio;

    while (running_.load()) {
        std::string path = "/_matrix/client/v3/sync?timeout=30000";
        if (!next_batch_.empty()) {
            path += "&since=" + next_batch_;
        }

        auto response = co_await http_.get(path);
        if (!response) {
            LOG_WARN("[matrix] Sync failed: {}", response.error().what());
            boost::asio::steady_timer timer(ioc_, std::chrono::seconds(5));
            co_await timer.async_wait(net::use_awaitable);
            continue;
        }

        if (!response->is_success()) {
            LOG_WARN("[matrix] Sync returned status {}", response->status);
            boost::asio::steady_timer timer(ioc_, std::chrono::seconds(5));
            co_await timer.async_wait(net::use_awaitable);
            continue;
        }

        try {
            auto body = json::parse(response->body);
            next_batch_ = body.value("next_batch", "");

            // Process room events
            if (body.contains("rooms") && body["rooms"].contains("join")) {
                for (auto& [room_id, room_data] : body["rooms"]["join"].items()) {
                    if (!room_data.contains("timeline") ||
                        !room_data["timeline"].contains("events")) {
                        continue;
                    }

                    for (const auto& event : room_data["timeline"]["events"]) {
                        std::string event_type = event.value("type", "");
                        if (event_type != "m.room.message") continue;

                        // Skip own messages
                        std::string sender = event.value("sender", "");
                        if (config_.user_id && sender == *config_.user_id) continue;

                        IncomingMessage incoming;
                        incoming.id = event.value("event_id", "");
                        incoming.channel = config_.channel_name;
                        incoming.sender_id = sender;
                        incoming.sender_name = sender;
                        incoming.received_at = Clock::now();
                        incoming.raw = event;
                        incoming.raw["_room_id"] = room_id;

                        if (event.contains("content")) {
                            incoming.text = event["content"].value("body", "");
                        }

                        dispatch(std::move(incoming));
                    }
                }
            }
        } catch (const json::exception& e) {
            LOG_ERROR("[matrix] JSON parse error in sync: {}", e.what());
        }
    }

    LOG_DEBUG("[matrix] Exiting sync loop");
}

// ---------------------------------------------------------------------------
// v2026.3.31: Draft streaming — send and edit messages in place
// ---------------------------------------------------------------------------

auto MatrixChannel::send_draft(std::string_view room_id, std::string_view text)
    -> boost::asio::awaitable<openclaw::Result<std::string>>
{
    auto txn_id = utils::generate_id(16);
    std::string path = "/_matrix/client/v3/rooms/" +
                       std::string(room_id) +
                       "/send/m.room.message/" + txn_id;

    json payload = {
        {"msgtype", "m.text"},
        {"body", std::string(text)},
    };

    auto response = co_await http_.put(path, payload.dump());
    if (!response) {
        co_return make_fail(response.error());
    }
    if (!response->is_success()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError,
                       "Matrix send_draft failed",
                       "status=" + std::to_string(response->status)));
    }

    try {
        auto body = json::parse(response->body);
        co_return body.value("event_id", "");
    } catch (const json::exception& e) {
        co_return make_fail(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse Matrix send response", e.what()));
    }
}

auto MatrixChannel::edit_message(std::string_view room_id,
                                  std::string_view event_id,
                                  std::string_view new_text)
    -> boost::asio::awaitable<openclaw::Result<void>>
{
    auto txn_id = utils::generate_id(16);
    std::string path = "/_matrix/client/v3/rooms/" +
                       std::string(room_id) +
                       "/send/m.room.message/" + txn_id;

    json payload = {
        {"msgtype", "m.text"},
        {"body", "* " + std::string(new_text)},
        {"m.new_content", {
            {"msgtype", "m.text"},
            {"body", std::string(new_text)},
        }},
        {"m.relates_to", {
            {"rel_type", "m.replace"},
            {"event_id", std::string(event_id)},
        }},
    };

    auto response = co_await http_.put(path, payload.dump());
    if (!response) {
        co_return make_fail(response.error());
    }
    if (!response->is_success()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError,
                       "Matrix edit_message failed",
                       "status=" + std::to_string(response->status)));
    }

    co_return ok_result();
}

auto MatrixChannel::fetch_room_history(std::string_view room_id, int limit)
    -> boost::asio::awaitable<openclaw::Result<json>>
{
    std::string path = "/_matrix/client/v3/rooms/" +
                       std::string(room_id) +
                       "/messages?dir=b&limit=" + std::to_string(limit);

    // v2026.3.31: Per-room watermarks for retry-safe snapshots
    auto it = room_history_watermarks_.find(std::string(room_id));
    if (it != room_history_watermarks_.end()) {
        path += "&from=" + it->second;
    }

    auto response = co_await http_.get(path);
    if (!response) {
        co_return make_fail(response.error());
    }
    if (!response->is_success()) {
        co_return make_fail(
            make_error(ErrorCode::ChannelError,
                       "Matrix fetch_room_history failed",
                       "status=" + std::to_string(response->status)));
    }

    try {
        auto body = json::parse(response->body);
        // Update watermark for next fetch
        if (body.contains("end")) {
            room_history_watermarks_[std::string(room_id)] =
                body["end"].get<std::string>();
        }
        co_return body;
    } catch (const json::exception& e) {
        co_return make_fail(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse Matrix messages response", e.what()));
    }
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

auto make_matrix_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>
{
    MatrixConfig config;
    config.homeserver_url = settings.value("homeserver_url", "");
    config.access_token = settings.value("access_token", "");
    config.channel_name = settings.value("channel_name", "matrix");

    if (settings.contains("user_id")) {
        config.user_id = settings.at("user_id").get<std::string>();
    }
    if (settings.contains("room_bindings")) {
        config.room_bindings = settings["room_bindings"];
    }

    // v2026.3.31: History limit, proxy, draft streaming, thread replies
    if (settings.contains("history_limit")) {
        config.history_limit = settings.at("history_limit").get<int>();
    }
    if (settings.contains("proxy")) {
        config.proxy = settings.at("proxy").get<std::string>();
    }
    config.draft_streaming = settings.value("draft_streaming", false);
    if (settings.contains("thread_replies")) {
        config.thread_replies = settings["thread_replies"];
    }

    // v2026.4.1: Bot and private network settings
    if (settings.contains("allow_bots")) {
        config.allow_bots = settings.at("allow_bots").get<std::string>();
    }
    config.allow_private_network = settings.value("allow_private_network", false);

    if (config.homeserver_url.empty() || config.access_token.empty()) {
        LOG_ERROR("[matrix] homeserver_url and access_token are required in channel settings");
        return nullptr;
    }

    return std::make_unique<MatrixChannel>(std::move(config), ioc);
}

} // namespace openclaw::channels
