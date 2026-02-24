#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"
#include "openclaw/core/types.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;

/// A JSON-RPC style request frame sent from client to server.
struct RequestFrame {
    std::string id;
    std::string method;
    json params;
};

void to_json(json& j, const RequestFrame& f);
void from_json(const json& j, RequestFrame& f);

/// A JSON-RPC style response frame sent from server to client.
struct ResponseFrame {
    std::string id;
    bool ok = true;
    std::optional<json> result;
    std::optional<json> error;

    [[nodiscard]] auto is_error() const noexcept -> bool {
        return error.has_value();
    }
};

void to_json(json& j, const ResponseFrame& f);
void from_json(const json& j, ResponseFrame& f);

/// A server-initiated event pushed to connected clients.
struct EventFrame {
    std::string event;
    json data;
};

void to_json(json& j, const EventFrame& f);
void from_json(const json& j, EventFrame& f);

/// Discriminated union of all gateway frame types.
using Frame = std::variant<RequestFrame, ResponseFrame, EventFrame>;

/// Parse a raw JSON string into a typed Frame.
/// Returns an error if the JSON is malformed or the frame type cannot be
/// determined.
auto parse_frame(std::string_view data) -> Result<Frame>;

/// Serialize a Frame back to a JSON string for transmission.
auto serialize_frame(const Frame& frame) -> std::string;

/// Build a success ResponseFrame for a given request id.
auto make_response(const std::string& id, json result) -> ResponseFrame;

/// Build an error ResponseFrame for a given request id.
auto make_error_response(const std::string& id, ErrorCode code,
                         std::string_view message) -> ResponseFrame;

/// Build an EventFrame.
auto make_event(std::string event, json data = json::object()) -> EventFrame;

} // namespace openclaw::gateway
