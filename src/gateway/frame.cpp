#include "openclaw/gateway/frame.hpp"

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::gateway {

// -- RequestFrame serialization --

void to_json(json& j, const RequestFrame& f) {
    j = json{
        {"type", "request"},
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
        {"type", "response"},
        {"id", f.id},
    };
    if (f.result) j["result"] = *f.result;
    if (f.error) j["error"] = *f.error;
}

void from_json(const json& j, ResponseFrame& f) {
    j.at("id").get_to(f.id);
    if (j.contains("result")) f.result = j.at("result");
    if (j.contains("error")) f.error = j.at("error");
}

// -- EventFrame serialization --

void to_json(json& j, const EventFrame& f) {
    j = json{
        {"type", "event"},
        {"event", f.event},
        {"data", f.data},
    };
}

void from_json(const json& j, EventFrame& f) {
    j.at("event").get_to(f.event);
    if (j.contains("data")) {
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
    } else if (j.contains("id") && (j.contains("result") || j.contains("error"))) {
        type = "response";
    } else {
        return std::unexpected(
            make_error(ErrorCode::ProtocolError,
                       "Cannot determine frame type from JSON"));
    }

    try {
        if (type == "request") {
            RequestFrame f;
            from_json(j, f);
            return Frame{std::move(f)};
        }
        if (type == "response") {
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
        .result = std::move(result),
        .error = std::nullopt,
    };
}

auto make_error_response(const std::string& id, ErrorCode code,
                         std::string_view message) -> ResponseFrame {
    return ResponseFrame{
        .id = id,
        .result = std::nullopt,
        .error = json{
            {"code", static_cast<int>(code)},
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
