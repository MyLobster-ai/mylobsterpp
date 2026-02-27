#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio.hpp>

#include "openclaw/core/error.hpp"

namespace openclaw::infra {

/// Callback invoked for each chunk of response data during streaming.
/// Return true to continue receiving data, false to abort the request.
using HttpChunkCallback = std::function<bool(const char* data, size_t length)>;

/// HTTP response from the client.
struct HttpResponse {
    int status = 0;
    std::map<std::string, std::string> headers;
    std::string body;

    /// Returns true if the status code indicates success (2xx).
    [[nodiscard]] auto is_success() const noexcept -> bool {
        return status >= 200 && status < 300;
    }
};

/// Configuration for the HTTP client.
struct HttpClientConfig {
    std::string base_url;
    int timeout_seconds = 30;
    bool verify_ssl = true;
    std::map<std::string, std::string> default_headers;
};

/// Asynchronous HTTP client wrapping cpp-httplib.
/// Provides awaitable methods compatible with boost::asio coroutines.
class HttpClient {
public:
    explicit HttpClient(boost::asio::io_context& ioc, HttpClientConfig config);
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    HttpClient(HttpClient&&) noexcept;
    HttpClient& operator=(HttpClient&&) noexcept;

    /// Performs an asynchronous HTTP GET request.
    auto get(std::string_view path,
             const std::map<std::string, std::string>& headers = {})
        -> boost::asio::awaitable<openclaw::Result<HttpResponse>>;

    /// Performs an asynchronous HTTP POST request.
    auto post(std::string_view path,
              std::string_view body,
              std::string_view content_type = "application/json",
              const std::map<std::string, std::string>& headers = {})
        -> boost::asio::awaitable<openclaw::Result<HttpResponse>>;

    /// Performs a streaming HTTP POST request on a background thread.
    /// The chunk_callback is invoked for each chunk of the response body
    /// as it arrives (on the background thread). For error responses
    /// (non-2xx), the body is buffered and returned in HttpResponse::body.
    auto post_stream(std::string_view path,
                     std::string_view body,
                     std::string_view content_type,
                     const std::map<std::string, std::string>& headers,
                     HttpChunkCallback chunk_callback)
        -> boost::asio::awaitable<openclaw::Result<HttpResponse>>;

    /// Performs an asynchronous HTTP PUT request.
    auto put(std::string_view path,
             std::string_view body,
             std::string_view content_type = "application/json",
             const std::map<std::string, std::string>& headers = {})
        -> boost::asio::awaitable<openclaw::Result<HttpResponse>>;

    /// Performs an asynchronous HTTP DELETE request.
    auto delete_(std::string_view path,
                 const std::map<std::string, std::string>& headers = {})
        -> boost::asio::awaitable<openclaw::Result<HttpResponse>>;

    /// Sets a default header that will be sent with every request.
    void set_default_header(std::string key, std::string value);

    /// Returns the base URL.
    [[nodiscard]] auto base_url() const -> const std::string&;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace openclaw::infra
