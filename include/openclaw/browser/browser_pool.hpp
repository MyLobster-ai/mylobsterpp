#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "openclaw/browser/cdp_client.hpp"
#include "openclaw/core/config.hpp"
#include "openclaw/core/error.hpp"

namespace openclaw::browser {

using boost::asio::awaitable;

/// Represents a single managed browser instance.
struct BrowserInstance {
    std::string id;
    std::unique_ptr<CdpClient> cdp;
    std::string ws_endpoint;
    pid_t pid = 0;
    bool in_use = false;
    int64_t last_used = 0;
};

/// Pool manager for Chrome browser instances.
/// Handles launching, reusing, and closing Chrome processes with CDP.
class BrowserPool {
public:
    BrowserPool(boost::asio::io_context& ioc, const BrowserConfig& config);
    ~BrowserPool();

    BrowserPool(const BrowserPool&) = delete;
    BrowserPool& operator=(const BrowserPool&) = delete;
    BrowserPool(BrowserPool&&) noexcept;
    BrowserPool& operator=(BrowserPool&&) noexcept;

    /// Acquire a browser instance from the pool.
    /// Launches a new Chrome process if the pool is empty and below max capacity.
    auto acquire() -> awaitable<Result<BrowserInstance*>>;

    /// Release a browser instance back to the pool.
    void release(BrowserInstance* instance);

    /// Close a specific browser instance and remove it from the pool.
    auto close(std::string_view instance_id) -> awaitable<Result<void>>;

    /// Close all browser instances in the pool.
    auto close_all() -> awaitable<void>;

    /// Returns the number of active (in-use) instances.
    [[nodiscard]] auto active_count() const -> size_t;

    /// Returns the total number of instances (active + idle).
    [[nodiscard]] auto total_count() const -> size_t;

    /// Returns the maximum pool size.
    [[nodiscard]] auto max_size() const -> size_t;

private:
    auto launch_browser() -> Result<std::unique_ptr<BrowserInstance>>;
    auto find_chrome() const -> std::string;
    auto get_ws_endpoint(int debug_port) -> Result<std::string>;
    auto allocate_debug_port() -> int;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace openclaw::browser
