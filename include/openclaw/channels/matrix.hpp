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

/// v2026.3.7: Configuration for the Matrix channel.
struct MatrixConfig {
    std::string homeserver_url;
    std::string access_token;
    std::string channel_name = "matrix";
    std::optional<std::string> user_id;

    // v2026.3.7: Room bindings for agent selection
    json room_bindings;  // Maps room IDs to agent configs

    // v2026.3.31: Optional room history context
    std::optional<int> history_limit;  // Max messages to fetch for room context

    // v2026.3.31: HTTP(S) proxy support
    std::optional<std::string> proxy;  // HTTP(S) proxy URL for homeserver connections

    // v2026.3.31: Draft streaming — update same message in place
    bool draft_streaming = false;

    // v2026.3.31: Per-DM thread reply overrides
    json thread_replies;  // Per-room threadReplies overrides with isolation

    // v2026.4.1: Allow bot users
    std::optional<std::string> allow_bots;  // "none", "mentions", "all"

    // v2026.4.1: Allow private network homeservers per account
    bool allow_private_network = false;
};

/// v2026.3.7: Matrix channel implementation.
/// Supports safer m.direct fallback detection and explicit room bindings.
class MatrixChannel : public Channel {
public:
    MatrixChannel(MatrixConfig config, boost::asio::io_context& ioc);
    ~MatrixChannel() override = default;

    auto start() -> boost::asio::awaitable<void> override;
    auto stop() -> boost::asio::awaitable<void> override;
    auto send(OutgoingMessage msg) -> boost::asio::awaitable<openclaw::Result<void>> override;

    [[nodiscard]] auto name() const -> std::string_view override { return config_.channel_name; }
    [[nodiscard]] auto type() const -> std::string_view override { return "matrix"; }
    [[nodiscard]] auto is_running() const noexcept -> bool override { return running_.load(); }

    /// v2026.3.7: Check if a room is bound to a specific agent.
    [[nodiscard]] auto resolve_room_agent(std::string_view room_id) const
        -> std::optional<std::string>;

    /// v2026.3.7: Safer m.direct fallback detection.
    /// Honors explicit room bindings over DM classification.
    [[nodiscard]] auto is_dm_room(std::string_view room_id) const -> bool;

    /// v2026.3.31: Send a draft message (for streaming updates in place).
    auto send_draft(std::string_view room_id, std::string_view text)
        -> boost::asio::awaitable<openclaw::Result<std::string>>;

    /// v2026.3.31: Edit a previously sent message (for draft streaming).
    auto edit_message(std::string_view room_id,
                      std::string_view event_id,
                      std::string_view new_text)
        -> boost::asio::awaitable<openclaw::Result<void>>;

    /// v2026.3.31: Fetch room history for context.
    auto fetch_room_history(std::string_view room_id, int limit)
        -> boost::asio::awaitable<openclaw::Result<json>>;

private:
    /// Matrix sync loop.
    auto sync_loop() -> boost::asio::awaitable<void>;

    MatrixConfig config_;
    boost::asio::io_context& ioc_;
    infra::HttpClient http_;
    std::atomic<bool> running_{false};
    std::string next_batch_;

    // v2026.3.31: Per-room watermarks for history context
    std::unordered_map<std::string, std::string> room_history_watermarks_;
};

/// Creates a MatrixChannel from generic ChannelConfig settings JSON.
auto make_matrix_channel(const json& settings, boost::asio::io_context& ioc)
    -> std::unique_ptr<Channel>;

} // namespace openclaw::channels
