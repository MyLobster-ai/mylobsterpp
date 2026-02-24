#include "openclaw/gateway/frame.hpp"

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::gateway {

// -- RequestFrame serialization --

void to_json(json& j, const RequestFrame& f) {
    j = json{
        {"type", "req"},
        {"id", f.id},
        {"method", f.method},
        {"params", f.params},
    };
}

void from_json(const json& j, RequestFrame& f) {
    j.at("id").get_to(f.id);
    j.at("method").get_to(f.method);
    if (j.contains("params")) {
        f.params = j.at("params");
    } else {
        f.params = json::object();
    }
}

// -- ResponseFrame serialization --

void to_json(json& j, const ResponseFrame& f) {
    j = json{
        {"type", "res"},
        {"id", f.id},
        {"ok", f.ok},
    };
    if (f.result) j["payload"] = *f.result;
    if (f.error) j["error"] = *f.error;
}

void from_json(const json& j, ResponseFrame& f) {
    j.at("id").get_to(f.id);
    f.ok = j.value("ok", true);
    // Accept both "payload" (OpenClaw v2026.2.22) and "result" (legacy)
    if (j.contains("payload")) {
        f.result = j.at("payload");
    } else if (j.contains("result")) {
        f.result = j.at("result");
    }
    if (j.contains("error")) f.error = j.at("error");
}

// -- EventFrame serialization --

void to_json(json& j, const EventFrame& f) {
    j = json{
        {"type", "event"},
        {"event", f.event},
        {"payload", f.data},
    };
}

void from_json(const json& j, EventFrame& f) {
    j.at("event").get_to(f.event);
    if (j.contains("payload")) {
        f.data = j.at("payload");
    } else if (j.contains("data")) {
        // Backwards compatibility: accept "data" field as well
        f.data = j.at("data");
    } else {
        f.data = json::object();
    }
}

// -- Frame parsing --

auto parse_frame(std::string_view data) -> Result<Frame> {
    json j;
    try {
        j = json::parse(data);
    } catch (const json::parse_error& e) {
        return std::unexpected(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse frame JSON", e.what()));
    }

    if (!j.is_object()) {
        return std::unexpected(
            make_error(ErrorCode::ProtocolError,
                       "Frame must be a JSON object"));
    }

    // Determine frame type.
    // Explicit "type" field takes precedence; otherwise infer from shape.
    std::string type;
    if (j.contains("type") && j["type"].is_string()) {
        type = j["type"].get<std::string>();
    } else if (j.contains("method")) {
        type = "request";
    } else if (j.contains("event")) {
        type = "event";
    } else if (j.contains("id") && (j.contains("payload") || j.contains("result") || j.contains("error"))) {
        type = "response";
    } else {
        return std::unexpected(
            make_error(ErrorCode::ProtocolError,
                       "Cannot determine frame type from JSON"));
    }

    try {
        if (type == "req" || type == "request") {
            RequestFrame f;
            from_json(j, f);
            return Frame{std::move(f)};
        }
        if (type == "res" || type == "response") {
            ResponseFrame f;
            from_json(j, f);
            return Frame{std::move(f)};
        }
        if (type == "event") {
            EventFrame f;
            from_json(j, f);
            return Frame{std::move(f)};
        }
    } catch (const json::exception& e) {
        return std::unexpected(
            make_error(ErrorCode::SerializationError,
                       "Failed to deserialize frame fields", e.what()));
    }

    return std::unexpected(
        make_error(ErrorCode::ProtocolError,
                   "Unknown frame type: " + type));
}

// -- Frame serialization --

auto serialize_frame(const Frame& frame) -> std::string {
    json j;
    std::visit([&j](const auto& f) { to_json(j, f); }, frame);
    return j.dump();
}

// -- Factory helpers --

auto make_response(const std::string& id, json result) -> ResponseFrame {
    return ResponseFrame{
        .id = id,
        .ok = true,
        .result = std::move(result),
        .error = std::nullopt,
    };
}

auto make_error_response(const std::string& id, ErrorCode code,
                         std::string_view message) -> ResponseFrame {
    return ResponseFrame{
        .id = id,
        .ok = false,
        .result = std::nullopt,
        .error = json{
            {"code", std::string(error_code_to_string(code))},
            {"message", std::string(message)},
        },
    };
}

auto make_event(std::string event, json data) -> EventFrame {
    return EventFrame{
        .event = std::move(event),
        .data = std::move(data),
    };
}

} // namespace openclaw::gateway
