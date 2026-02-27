#include "openclaw/infra/http_client.hpp"
#include "openclaw/core/logger.hpp"

#include <httplib.h>

#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace openclaw::infra {

namespace {

auto to_http_response(const httplib::Result& result)
    -> openclaw::Result<HttpResponse> {
    if (!result) {
        auto err = result.error();
        std::string detail;
        switch (err) {
            case httplib::Error::Connection:
                detail = "Connection failed";
                break;
            case httplib::Error::BindIPAddress:
                detail = "Bind IP address failed";
                break;
            case httplib::Error::Read:
                detail = "Read error";
                break;
            case httplib::Error::Write:
                detail = "Write error";
                break;
            case httplib::Error::ExceedRedirectCount:
                detail = "Exceeded redirect count";
                break;
            case httplib::Error::Canceled:
                detail = "Request canceled";
                break;
            case httplib::Error::SSLConnection:
                detail = "SSL connection error";
                break;
            case httplib::Error::SSLLoadingCerts:
                detail = "SSL certificate loading error";
                break;
            case httplib::Error::SSLServerVerification:
                detail = "SSL server verification failed";
                break;
            case httplib::Error::ConnectionTimeout:
                return std::unexpected(
                    openclaw::make_error(ErrorCode::Timeout,
                                        "HTTP request timed out",
                                        "Connection timeout"));
            default:
                detail = "Unknown HTTP error";
                break;
        }
        return std::unexpected(
            openclaw::make_error(ErrorCode::ConnectionFailed,
                                "HTTP request failed", detail));
    }

    HttpResponse response;
    response.status = result->status;
    response.body = result->body;

    for (const auto& [key, value] : result->headers) {
        response.headers[key] = value;
    }

    return response;
}

} // anonymous namespace

struct HttpClient::Impl {
    boost::asio::io_context& ioc;
    HttpClientConfig config;
    std::unique_ptr<httplib::Client> client;

    Impl(boost::asio::io_context& ioc_, HttpClientConfig config_)
        : ioc(ioc_), config(std::move(config_)) {
        client = std::make_unique<httplib::Client>(config.base_url);
        client->set_connection_timeout(config.timeout_seconds);
        client->set_read_timeout(config.timeout_seconds);
        client->set_write_timeout(config.timeout_seconds);

        if (!config.verify_ssl) {
            client->enable_server_certificate_verification(false);
        }

        // Set default headers
        httplib::Headers hdrs;
        for (const auto& [key, value] : config.default_headers) {
            hdrs.emplace(key, value);
        }
        client->set_default_headers(hdrs);

        LOG_DEBUG("HTTP client created for {}", config.base_url);
    }

    auto merge_headers(const std::map<std::string, std::string>& extra)
        -> httplib::Headers {
        httplib::Headers hdrs;
        for (const auto& [k, v] : extra) {
            hdrs.emplace(k, v);
        }
        return hdrs;
    }
};

HttpClient::HttpClient(boost::asio::io_context& ioc, HttpClientConfig config)
    : impl_(std::make_unique<Impl>(ioc, std::move(config))) {}

HttpClient::~HttpClient() = default;

HttpClient::HttpClient(HttpClient&&) noexcept = default;
HttpClient& HttpClient::operator=(HttpClient&&) noexcept = default;

auto HttpClient::get(std::string_view path,
                     const std::map<std::string, std::string>& headers)
    -> boost::asio::awaitable<openclaw::Result<HttpResponse>> {
    auto executor = co_await boost::asio::this_coro::executor;

    // Offload the synchronous httplib call to a thread pool
    auto result = co_await boost::asio::co_spawn(
        boost::asio::make_strand(impl_->ioc),
        [this, p = std::string(path), hdrs = impl_->merge_headers(headers)]()
            -> boost::asio::awaitable<openclaw::Result<HttpResponse>> {
            LOG_DEBUG("GET {}{}", impl_->config.base_url, p);
            auto res = impl_->client->Get(p, hdrs);
            co_return to_http_response(res);
        },
        boost::asio::use_awaitable
    );

    co_return result;
}

auto HttpClient::post(std::string_view path,
                      std::string_view body,
                      std::string_view content_type,
                      const std::map<std::string, std::string>& headers)
    -> boost::asio::awaitable<openclaw::Result<HttpResponse>> {
    auto result = co_await boost::asio::co_spawn(
        boost::asio::make_strand(impl_->ioc),
        [this, p = std::string(path), b = std::string(body),
         ct = std::string(content_type),
         hdrs = impl_->merge_headers(headers)]()
            -> boost::asio::awaitable<openclaw::Result<HttpResponse>> {
            LOG_DEBUG("POST {}{}", impl_->config.base_url, p);
            auto res = impl_->client->Post(p, hdrs, b, ct);
            co_return to_http_response(res);
        },
        boost::asio::use_awaitable
    );

    co_return result;
}

auto HttpClient::put(std::string_view path,
                     std::string_view body,
                     std::string_view content_type,
                     const std::map<std::string, std::string>& headers)
    -> boost::asio::awaitable<openclaw::Result<HttpResponse>> {
    auto result = co_await boost::asio::co_spawn(
        boost::asio::make_strand(impl_->ioc),
        [this, p = std::string(path), b = std::string(body),
         ct = std::string(content_type),
         hdrs = impl_->merge_headers(headers)]()
            -> boost::asio::awaitable<openclaw::Result<HttpResponse>> {
            LOG_DEBUG("PUT {}{}", impl_->config.base_url, p);
            auto res = impl_->client->Put(p, hdrs, b, ct);
            co_return to_http_response(res);
        },
        boost::asio::use_awaitable
    );

    co_return result;
}

auto HttpClient::delete_(std::string_view path,
                         const std::map<std::string, std::string>& headers)
    -> boost::asio::awaitable<openclaw::Result<HttpResponse>> {
    auto result = co_await boost::asio::co_spawn(
        boost::asio::make_strand(impl_->ioc),
        [this, p = std::string(path), hdrs = impl_->merge_headers(headers)]()
            -> boost::asio::awaitable<openclaw::Result<HttpResponse>> {
            LOG_DEBUG("DELETE {}{}", impl_->config.base_url, p);
            auto res = impl_->client->Delete(p, hdrs);
            co_return to_http_response(res);
        },
        boost::asio::use_awaitable
    );

    co_return result;
}

auto HttpClient::post_stream(std::string_view path,
                             std::string_view body,
                             std::string_view content_type,
                             const std::map<std::string, std::string>& headers,
                             HttpChunkCallback chunk_cb)
    -> boost::asio::awaitable<openclaw::Result<HttpResponse>> {

    // Shared state between background thread and coroutine.
    struct StreamState {
        std::mutex mtx;
        std::optional<openclaw::Result<HttpResponse>> result;
    };

    auto state = std::make_shared<StreamState>();
    auto timer = std::make_shared<boost::asio::steady_timer>(
        impl_->ioc, boost::asio::steady_timer::time_point::max());

    // Capture config for fresh httplib::Client on background thread.
    auto base_url = impl_->config.base_url;
    auto timeout = impl_->config.timeout_seconds;
    auto verify_ssl = impl_->config.verify_ssl;
    auto default_headers = impl_->config.default_headers;

    std::thread([
        state, timer,
        chunk_cb = std::move(chunk_cb),
        p = std::string(path),
        b = std::string(body),
        ct = std::string(content_type),
        extra_hdrs = impl_->merge_headers(headers),
        base_url = std::move(base_url),
        timeout, verify_ssl,
        default_headers = std::move(default_headers)
    ]() mutable {
        LOG_DEBUG("post_stream: background thread started for {}{}", base_url, p);

        // Create fresh client (httplib::Client is not thread-safe).
        httplib::Client client(base_url);
        client.set_connection_timeout(timeout);
        client.set_read_timeout(timeout);
        client.set_write_timeout(timeout);
        if (!verify_ssl) {
            client.enable_server_certificate_verification(false);
        }

        httplib::Headers default_hdrs;
        for (const auto& [k, v] : default_headers) {
            default_hdrs.emplace(k, v);
        }
        client.set_default_headers(default_hdrs);

        // Build request with content_receiver for streaming.
        httplib::Request req;
        req.method = "POST";
        req.path = p;
        req.headers = extra_hdrs;
        req.body = b;
        req.set_header("Content-Type", ct);

        // Track status to distinguish success (stream) vs error (buffer).
        int status_code = 0;
        std::string error_body;

        req.response_handler = [&status_code](const httplib::Response& r) -> bool {
            status_code = r.status;
            return true;
        };

        req.content_receiver =
            [&chunk_cb, &error_body, &status_code](
                const char* data, size_t data_length,
                uint64_t /*offset*/, uint64_t /*total_length*/) -> bool {
                if (status_code >= 200 && status_code < 300) {
                    return chunk_cb(data, data_length);
                } else {
                    // Buffer error response body for caller.
                    error_body.append(data, data_length);
                    return true;
                }
            };

        httplib::Response res;
        httplib::Error error = httplib::Error::Success;
        bool ok = client.send(req, res, error);

        // Build result.
        openclaw::Result<HttpResponse> result;
        if (!ok) {
            if (error == httplib::Error::ConnectionTimeout) {
                result = std::unexpected(
                    openclaw::make_error(ErrorCode::Timeout,
                                        "HTTP streaming request timed out",
                                        "Connection timeout"));
            } else {
                result = std::unexpected(
                    openclaw::make_error(ErrorCode::ConnectionFailed,
                                        "HTTP streaming request failed",
                                        "httplib error code " +
                                            std::to_string(static_cast<int>(error))));
            }
        } else {
            HttpResponse http_resp;
            http_resp.status = res.status;
            http_resp.body = std::move(error_body);
            for (const auto& [k, v] : res.headers) {
                http_resp.headers[k] = v;
            }
            result = std::move(http_resp);
        }

        {
            std::lock_guard lock(state->mtx);
            state->result = std::move(result);
        }
        // Post cancel to the timer's executor for thread safety.
        boost::asio::post(timer->get_executor(),
                          [timer] { timer->cancel(); });

        LOG_DEBUG("post_stream: background thread finished, status={}",
                  status_code);
    }).detach();

    // Suspend coroutine until background thread completes.
    boost::system::error_code ec;
    co_await timer->async_wait(
        boost::asio::redirect_error(boost::asio::use_awaitable, ec));

    std::lock_guard lock(state->mtx);
    if (!state->result.has_value()) {
        // Timer was cancelled by io_context shutdown, not by our thread.
        co_return std::unexpected(
            openclaw::make_error(ErrorCode::ConnectionClosed,
                                "HTTP streaming request was cancelled",
                                ec.message()));
    }
    co_return std::move(*state->result);
}

void HttpClient::set_default_header(std::string key, std::string value) {
    impl_->config.default_headers[std::move(key)] = std::move(value);

    // Rebuild the default headers on the underlying client
    httplib::Headers hdrs;
    for (const auto& [k, v] : impl_->config.default_headers) {
        hdrs.emplace(k, v);
    }
    impl_->client->set_default_headers(hdrs);
}

auto HttpClient::base_url() const -> const std::string& {
    return impl_->config.base_url;
}

} // namespace openclaw::infra
