#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace openclaw {

using json = nlohmann::json;
using Clock = std::chrono::system_clock;
using Timestamp = std::chrono::time_point<Clock>;

enum class Role {
    User,
    Assistant,
    System,
    Tool,
};

NLOHMANN_JSON_SERIALIZE_ENUM(Role, {
    {Role::User, "user"},
    {Role::Assistant, "assistant"},
    {Role::System, "system"},
    {Role::Tool, "tool"},
})

struct ContentBlock {
    std::string type;  // "text", "image", "tool_use", "tool_result"
    std::string text;
    std::optional<std::string> tool_use_id;
    std::optional<std::string> tool_name;
    std::optional<json> tool_input;
    std::optional<json> tool_result;
};

void to_json(json& j, const ContentBlock& c);
void from_json(const json& j, ContentBlock& c);

struct Message {
    std::string id;
    Role role;
    std::vector<ContentBlock> content;
    std::optional<std::string> model;
    Timestamp created_at;
};

void to_json(json& j, const Message& m);
void from_json(const json& j, Message& m);

struct Conversation {
    std::string id;
    std::string title;
    std::vector<Message> messages;
    Timestamp created_at;
    Timestamp updated_at;
};

struct Session {
    std::string id;
    std::string user_id;
    std::string device_id;
    std::optional<std::string> channel;
    std::optional<std::string> agent_id;  // Multi-agent support
    Timestamp created_at;
    Timestamp last_active;

    /// Builds a session key in the format "{agent_id}:{channel}:{peer_id}".
    /// Falls back to "{channel}:{peer_id}" if no agent_id.
    [[nodiscard]] auto session_key(std::string_view peer_id) const -> std::string {
        std::string key;
        if (agent_id) {
            key += *agent_id + ":";
        }
        key += channel.value_or("default") + ":" + std::string(peer_id);
        return key;
    }
};

struct DeviceIdentity {
    std::string device_id;
    std::string hostname;
    std::string os;
    std::string arch;
    // Ed25519 crypto identity for v2026.2.22 device auth
    std::string public_key_pem;
    std::string private_key_pem;
    std::string public_key_raw_b64url;  // 32-byte raw key, base64url encoded
};

void to_json(json& j, const DeviceIdentity& d);
void from_json(const json& j, DeviceIdentity& d);

enum class BindMode {
    Loopback,
    All,
};

NLOHMANN_JSON_SERIALIZE_ENUM(BindMode, {
    {BindMode::Loopback, "loopback"},
    {BindMode::All, "all"},
})

enum class ThinkingMode {
    None,
    Basic,
    Extended,
};

NLOHMANN_JSON_SERIALIZE_ENUM(ThinkingMode, {
    {ThinkingMode::None, "none"},
    {ThinkingMode::Basic, "basic"},
    {ThinkingMode::Extended, "extended"},
})

} // namespace openclaw
