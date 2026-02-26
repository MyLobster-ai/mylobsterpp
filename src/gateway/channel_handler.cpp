#include "openclaw/gateway/channel_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_channel_handlers(Protocol& protocol,
                               channels::ChannelRegistry& channels) {
    // channel.list
    protocol.register_method("channel.list",
        [&channels]([[maybe_unused]] json params) -> awaitable<json> {
            auto names = channels.list();
            json result = json::array();
            for (auto name : names) {
                auto* ch = channels.get(name);
                result.push_back(json{
                    {"name", std::string(name)},
                    {"type", ch ? std::string(ch->type()) : "unknown"},
                    {"connected", ch ? ch->is_running() : false},
                });
            }
            co_return json{{"channels", result}};
        },
        "List available communication channels", "channel");

    // channel.connect
    protocol.register_method("channel.connect",
        [&channels]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "name is required"}};
            }
            auto* ch = channels.get(name);
            if (!ch) {
                co_return json{{"ok", false}, {"error", "Channel not found: " + name}};
            }
            co_await ch->start();
            co_return json{{"ok", true}};
        },
        "Connect / enable a channel", "channel");

    // channel.disconnect
    protocol.register_method("channel.disconnect",
        [&channels]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "name is required"}};
            }
            auto* ch = channels.get(name);
            if (!ch) {
                co_return json{{"ok", false}, {"error", "Channel not found: " + name}};
            }
            co_await ch->stop();
            co_return json{{"ok", true}};
        },
        "Disconnect / disable a channel", "channel");

    // channel.status
    protocol.register_method("channel.status",
        [&channels]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "name is required"}};
            }
            auto* ch = channels.get(name);
            if (!ch) {
                co_return json{{"ok", false}, {"error", "Channel not found: " + name}};
            }
            co_return json{
                {"ok", true},
                {"name", name},
                {"connected", ch->is_running()},
                {"type", std::string(ch->type())},
            };
        },
        "Get channel connection status", "channel");

    // channel.send
    protocol.register_method("channel.send",
        [&channels]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("channel", "");
            auto to = params.value("to", "");
            auto text = params.value("text", "");
            if (name.empty() || text.empty()) {
                co_return json{{"ok", false}, {"error", "channel and text are required"}};
            }
            auto* ch = channels.get(name);
            if (!ch) {
                co_return json{{"ok", false}, {"error", "Channel not found: " + name}};
            }
            channels::OutgoingMessage msg;
            msg.channel = name;
            msg.recipient_id = to;
            msg.text = text;
            auto send_result = co_await ch->send(std::move(msg));
            if (!send_result.has_value()) {
                co_return json{{"ok", false}, {"error", send_result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Send a message through a channel", "channel");

    // channel.receive
    protocol.register_method("channel.receive",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // Messages are pushed via the global callback; this is a poll endpoint.
            co_return json{{"ok", true}, {"messages", json::array()}};
        },
        "Poll for messages from a channel", "channel");

    // channel.configure
    protocol.register_method("channel.configure",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // TODO: runtime channel configuration.
            co_return json{{"ok", true}};
        },
        "Update channel configuration", "channel");

    // channel.telegram.webhook
    protocol.register_method("channel.telegram.webhook",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // TODO: register Telegram webhook URL.
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Register Telegram webhook", "channel");

    // channel.discord.setup
    protocol.register_method("channel.discord.setup",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Set up Discord bot connection", "channel");

    // channel.slack.setup
    protocol.register_method("channel.slack.setup",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Set up Slack bot connection", "channel");

    // channel.whatsapp.setup
    protocol.register_method("channel.whatsapp.setup",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Set up WhatsApp Business API connection", "channel");

    // channel.sms.send
    protocol.register_method("channel.sms.send",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Send an SMS via Twilio", "channel");

    LOG_INFO("Registered channel handlers");
}

} // namespace openclaw::gateway
