#include "openclaw/channels/message.hpp"
#include "openclaw/channels/channel.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::channels {

// ---------------------------------------------------------------------------
// Attachment JSON serialization
// ---------------------------------------------------------------------------

void to_json(json& j, const Attachment& a) {
    j = json{
        {"type", a.type},
        {"url", a.url},
    };
    if (a.filename) j["filename"] = *a.filename;
    if (a.size) j["size"] = *a.size;
}

void from_json(const json& j, Attachment& a) {
    j.at("type").get_to(a.type);
    j.at("url").get_to(a.url);
    if (j.contains("filename")) a.filename = j.at("filename").get<std::string>();
    if (j.contains("size")) a.size = j.at("size").get<size_t>();
}

// ---------------------------------------------------------------------------
// IncomingMessage JSON serialization
// ---------------------------------------------------------------------------

void to_json(json& j, const IncomingMessage& m) {
    j = json{
        {"id", m.id},
        {"channel", m.channel},
        {"sender_id", m.sender_id},
        {"sender_name", m.sender_name},
        {"text", m.text},
        {"attachments", m.attachments},
        {"received_at", utils::timestamp_ms()},
    };
    if (m.reply_to) j["reply_to"] = *m.reply_to;
    if (m.thread_id) j["thread_id"] = *m.thread_id;
    if (!m.raw.is_null()) j["raw"] = m.raw;
}

void from_json(const json& j, IncomingMessage& m) {
    j.at("id").get_to(m.id);
    j.at("channel").get_to(m.channel);
    j.at("sender_id").get_to(m.sender_id);
    j.at("sender_name").get_to(m.sender_name);
    if (j.contains("text")) j.at("text").get_to(m.text);
    if (j.contains("attachments")) j.at("attachments").get_to(m.attachments);
    if (j.contains("reply_to")) m.reply_to = j.at("reply_to").get<std::string>();
    if (j.contains("thread_id")) m.thread_id = j.at("thread_id").get<std::string>();
    if (j.contains("raw")) m.raw = j.at("raw");
}

// ---------------------------------------------------------------------------
// OutgoingMessage JSON serialization
// ---------------------------------------------------------------------------

void to_json(json& j, const OutgoingMessage& m) {
    j = json{
        {"channel", m.channel},
        {"recipient_id", m.recipient_id},
        {"text", m.text},
        {"attachments", m.attachments},
    };
    if (m.reply_to) j["reply_to"] = *m.reply_to;
    if (m.thread_id) j["thread_id"] = *m.thread_id;
    if (!m.extra.is_null()) j["extra"] = m.extra;
}

void from_json(const json& j, OutgoingMessage& m) {
    j.at("channel").get_to(m.channel);
    j.at("recipient_id").get_to(m.recipient_id);
    if (j.contains("text")) j.at("text").get_to(m.text);
    if (j.contains("attachments")) j.at("attachments").get_to(m.attachments);
    if (j.contains("reply_to")) m.reply_to = j.at("reply_to").get<std::string>();
    if (j.contains("thread_id")) m.thread_id = j.at("thread_id").get<std::string>();
    if (j.contains("extra")) m.extra = j.at("extra");
}

} // namespace openclaw::channels
