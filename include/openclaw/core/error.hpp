#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace openclaw {

enum class ErrorCode {
    Unknown = 1,
    InvalidConfig,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    Unauthorized,
    Forbidden,
    Timeout,
    ConnectionFailed,
    ConnectionClosed,
    ProtocolError,
    SerializationError,
    IoError,
    DatabaseError,
    ProviderError,
    ChannelError,
    PluginError,
    BrowserError,
    MemoryError,
    SessionError,
    RateLimited,
    InternalError,
};

class Error {
public:
    Error(ErrorCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    Error(ErrorCode code, std::string message, std::string detail)
        : code_(code), message_(std::move(message)), detail_(std::move(detail)) {}

    [[nodiscard]] auto code() const noexcept -> ErrorCode { return code_; }
    [[nodiscard]] auto message() const noexcept -> std::string_view { return message_; }
    [[nodiscard]] auto detail() const noexcept -> std::string_view { return detail_; }

    [[nodiscard]] auto what() const -> std::string {
        if (detail_.empty()) return message_;
        return message_ + ": " + detail_;
    }

private:
    ErrorCode code_;
    std::string message_;
    std::string detail_;
};

template <typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;

inline auto make_error(ErrorCode code, std::string message) -> Error {
    return Error(code, std::move(message));
}

inline auto make_error(ErrorCode code, std::string message, std::string detail) -> Error {
    return Error(code, std::move(message), std::move(detail));
}

/// Convert ErrorCode to a string matching OpenClaw's error code format.
inline auto error_code_to_string(ErrorCode code) -> std::string_view {
    switch (code) {
        case ErrorCode::Unknown: return "UNKNOWN";
        case ErrorCode::InvalidConfig: return "INVALID_CONFIG";
        case ErrorCode::InvalidArgument: return "INVALID_ARGUMENT";
        case ErrorCode::NotFound: return "NOT_FOUND";
        case ErrorCode::AlreadyExists: return "ALREADY_EXISTS";
        case ErrorCode::Unauthorized: return "UNAUTHORIZED";
        case ErrorCode::Forbidden: return "FORBIDDEN";
        case ErrorCode::Timeout: return "TIMEOUT";
        case ErrorCode::ConnectionFailed: return "CONNECTION_FAILED";
        case ErrorCode::ConnectionClosed: return "CONNECTION_CLOSED";
        case ErrorCode::ProtocolError: return "PROTOCOL_ERROR";
        case ErrorCode::SerializationError: return "SERIALIZATION_ERROR";
        case ErrorCode::IoError: return "IO_ERROR";
        case ErrorCode::DatabaseError: return "DATABASE_ERROR";
        case ErrorCode::ProviderError: return "PROVIDER_ERROR";
        case ErrorCode::ChannelError: return "CHANNEL_ERROR";
        case ErrorCode::PluginError: return "PLUGIN_ERROR";
        case ErrorCode::BrowserError: return "BROWSER_ERROR";
        case ErrorCode::MemoryError: return "MEMORY_ERROR";
        case ErrorCode::SessionError: return "SESSION_ERROR";
        case ErrorCode::RateLimited: return "RATE_LIMITED";
        case ErrorCode::InternalError: return "INTERNAL_ERROR";
        default: return "UNKNOWN";
    }
}

} // namespace openclaw
