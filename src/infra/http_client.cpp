#include "openclaw/infra/http_client.hpp"
#include "openclaw/core/logger.hpp"

#include <httplib.h>

#include <memory>
#include <utility>

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
