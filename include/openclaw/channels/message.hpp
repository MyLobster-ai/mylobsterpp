#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "openclaw/core/types.hpp"

namespace openclaw::channels {

using json = nlohmann::json;
using openclaw::Timestamp;

/// Represents a file, image, audio, or video attachment on a channel message.
struct Attachment {
    std::string type;  // "image", "file", "audio", "video"
    std::string url;
    std::optional<std::string> filename;
    std::optional<size_t> size;
};

void to_json(json& j, const Attachment& a);
void from_json(const json& j, Attachment& a);

/// Maximum allowed media download size (50 MB).
inline constexpr size_t kMaxMediaDownloadBytes = 50 * 1024 * 1024;

/// A message received from a channel (platform -> agent).
struct IncomingMessage {
    std::string id;
    std::string channel;
    std::string sender_id;
    std::string sender_name;
    std::string text;
    std::vector<Attachment> attachments;
    std::optional<std::string> reply_to;
    std::optional<std::string> thread_id;
    json raw;  // original platform-specific data
    Timestamp received_at;
};

void to_json(json& j, const IncomingMessage& m);
void from_json(const json& j, IncomingMessage& m);

/// A message to be sent to a channel (agent -> platform).
struct OutgoingMessage {
    std::string channel;
    std::string recipient_id;
    std::string text;
    std::vector<Attachment> attachments;
    std::optional<std::string> reply_to;
    std::optional<std::string> thread_id;
    json extra;  // platform-specific extra payload
};

void to_json(json& j, const OutgoingMessage& m);
void from_json(const json& j, OutgoingMessage& m);

} // namespace openclaw::channels
