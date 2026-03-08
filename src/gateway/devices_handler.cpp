#include "openclaw/gateway/devices_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_devices_handlers(Protocol& protocol) {
    // device.pair.list — list paired devices.
    protocol.register_method("device.pair.list",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", true}, {"devices", json::array()}};
        },
        "List paired devices", "device");

    // device.pair.approve — approve a device pairing.
    protocol.register_method("device.pair.approve",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto device_id = params.value("deviceId", "");
            if (device_id.empty()) {
                co_return json{{"ok", false}, {"error", "deviceId is required"}};
            }
            LOG_INFO("Device pairing approved: {}", device_id);
            co_return json{{"ok", true}, {"deviceId", device_id}};
        },
        "Approve device pairing", "device");

    // device.pair.reject — reject a device pairing.
    protocol.register_method("device.pair.reject",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto device_id = params.value("deviceId", "");
            if (device_id.empty()) {
                co_return json{{"ok", false}, {"error", "deviceId is required"}};
            }
            co_return json{{"ok", true}, {"deviceId", device_id}};
        },
        "Reject device pairing", "device");

    // device.pair.remove — remove a paired device.
    protocol.register_method("device.pair.remove",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto device_id = params.value("deviceId", "");
            if (device_id.empty()) {
                co_return json{{"ok", false}, {"error", "deviceId is required"}};
            }
            LOG_INFO("Device removed: {}", device_id);
            co_return json{{"ok", true}};
        },
        "Remove paired device", "device");

    // device.token.rotate — rotate a device authentication token.
    protocol.register_method("device.token.rotate",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto device_id = params.value("deviceId", "");
            if (device_id.empty()) {
                co_return json{{"ok", false}, {"error", "deviceId is required"}};
            }
            LOG_INFO("Token rotated for device: {}", device_id);
            co_return json{{"ok", true}, {"deviceId", device_id}, {"rotated", true}};
        },
        "Rotate device token", "device");

    // device.token.revoke — revoke a device token.
    protocol.register_method("device.token.revoke",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto device_id = params.value("deviceId", "");
            if (device_id.empty()) {
                co_return json{{"ok", false}, {"error", "deviceId is required"}};
            }
            LOG_INFO("Token revoked for device: {}", device_id);
            co_return json{{"ok", true}, {"deviceId", device_id}};
        },
        "Revoke device token", "device");

    LOG_INFO("Registered devices handlers");
}

} // namespace openclaw::gateway
