#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <system_error>

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

} // namespace openclaw
