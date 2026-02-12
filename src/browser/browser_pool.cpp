#include "openclaw/browser/browser_pool.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"
#include "openclaw/infra/http_client.hpp"

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <vector>

#ifdef __APPLE__
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace openclaw::browser {

namespace fs = std::filesystem;
namespace net = boost::asio;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// BrowserPool::Impl
// ---------------------------------------------------------------------------

struct BrowserPool::Impl {
    net::io_context& ioc;
    BrowserConfig config;
    std::mutex pool_mutex;
    std::vector<std::unique_ptr<BrowserInstance>> instances;
    int next_debug_port = 9222;

    Impl(net::io_context& ctx, const BrowserConfig& cfg)
        : ioc(ctx), config(cfg) {}
};

// ---------------------------------------------------------------------------
// BrowserPool
// ---------------------------------------------------------------------------

BrowserPool::BrowserPool(boost::asio::io_context& ioc,
                         const BrowserConfig& config)
    : impl_(std::make_unique<Impl>(ioc, config)) {
    LOG_INFO("BrowserPool created (max_size={})", config.pool_size);
}

BrowserPool::~BrowserPool() {
    if (!impl_) return;

    // Kill all browser processes on destruction
    std::lock_guard lock(impl_->pool_mutex);
    for (auto& inst : impl_->instances) {
        if (inst->pid > 0) {
            ::kill(inst->pid, SIGTERM);
            int status = 0;
            ::waitpid(inst->pid, &status, WNOHANG);
        }
    }
    impl_->instances.clear();
}

BrowserPool::BrowserPool(BrowserPool&&) noexcept = default;
BrowserPool& BrowserPool::operator=(BrowserPool&&) noexcept = default;

auto BrowserPool::acquire() -> awaitable<Result<BrowserInstance*>> {
    std::lock_guard lock(impl_->pool_mutex);

    // Try to find an idle instance
    for (auto& inst : impl_->instances) {
        if (!inst->in_use) {
            inst->in_use = true;
            inst->last_used = utils::timestamp_ms();
            LOG_DEBUG("Reusing browser instance: {}", inst->id);
            co_return inst.get();
        }
    }

    // Check pool capacity
    if (impl_->instances.size() >= impl_->config.pool_size) {
        co_return std::unexpected(
            make_error(ErrorCode::BrowserError,
                       "Browser pool exhausted",
                       "max_size=" + std::to_string(impl_->config.pool_size)));
    }

    // Launch a new browser instance
    auto instance = launch_browser();
    if (!instance) {
        co_return std::unexpected(instance.error());
    }

    auto* ptr = instance->get();

    // Connect CDP client
    auto connect_result = co_await ptr->cdp->connect(ptr->ws_endpoint);
    if (!connect_result) {
        // Kill the process since we can't connect
        if (ptr->pid > 0) {
            ::kill(ptr->pid, SIGTERM);
        }
        co_return std::unexpected(connect_result.error());
    }

    // Enable necessary CDP domains
    auto page_result = co_await ptr->cdp->send_command("Page.enable");
    if (!page_result) {
        LOG_WARN("Failed to enable Page domain: {}", page_result.error().what());
    }

    auto runtime_result = co_await ptr->cdp->send_command("Runtime.enable");
    if (!runtime_result) {
        LOG_WARN("Failed to enable Runtime domain: {}",
                 runtime_result.error().what());
    }

    auto dom_result = co_await ptr->cdp->send_command("DOM.enable");
    if (!dom_result) {
        LOG_WARN("Failed to enable DOM domain: {}", dom_result.error().what());
    }

    ptr->in_use = true;
    ptr->last_used = utils::timestamp_ms();
    impl_->instances.push_back(std::move(*instance));

    LOG_INFO("Launched new browser instance: {}", ptr->id);
    co_return impl_->instances.back().get();
}

void BrowserPool::release(BrowserInstance* instance) {
    if (!instance) return;

    std::lock_guard lock(impl_->pool_mutex);
    instance->in_use = false;
    instance->last_used = utils::timestamp_ms();
    LOG_DEBUG("Released browser instance: {}", instance->id);
}

auto BrowserPool::close(std::string_view instance_id) -> awaitable<Result<void>> {
    std::lock_guard lock(impl_->pool_mutex);

    auto it = std::find_if(impl_->instances.begin(), impl_->instances.end(),
                           [&](const auto& inst) {
                               return inst->id == instance_id;
                           });

    if (it == impl_->instances.end()) {
        co_return std::unexpected(
            make_error(ErrorCode::NotFound,
                       "Browser instance not found",
                       std::string(instance_id)));
    }

    auto& inst = *it;

    // Disconnect CDP
    if (inst->cdp && inst->cdp->is_connected()) {
        co_await inst->cdp->disconnect();
    }

    // Kill the process
    if (inst->pid > 0) {
        ::kill(inst->pid, SIGTERM);
        int status = 0;
        ::waitpid(inst->pid, &status, WNOHANG);
    }

    impl_->instances.erase(it);
    LOG_INFO("Closed browser instance: {}", std::string(instance_id));
    co_return Result<void>{};
}

auto BrowserPool::close_all() -> awaitable<void> {
    std::lock_guard lock(impl_->pool_mutex);

    for (auto& inst : impl_->instances) {
        if (inst->cdp && inst->cdp->is_connected()) {
            co_await inst->cdp->disconnect();
        }
        if (inst->pid > 0) {
            ::kill(inst->pid, SIGTERM);
            int status = 0;
            ::waitpid(inst->pid, &status, WNOHANG);
        }
    }

    auto count = impl_->instances.size();
    impl_->instances.clear();
    LOG_INFO("Closed all {} browser instances", count);
}

auto BrowserPool::active_count() const -> size_t {
    std::lock_guard lock(impl_->pool_mutex);
    return std::count_if(impl_->instances.begin(), impl_->instances.end(),
                         [](const auto& inst) { return inst->in_use; });
}

auto BrowserPool::total_count() const -> size_t {
    std::lock_guard lock(impl_->pool_mutex);
    return impl_->instances.size();
}

auto BrowserPool::max_size() const -> size_t {
    return impl_->config.pool_size;
}

auto BrowserPool::launch_browser() -> Result<std::unique_ptr<BrowserInstance>> {
    auto chrome_path = find_chrome();
    if (chrome_path.empty()) {
        return std::unexpected(
            make_error(ErrorCode::BrowserError,
                       "Chrome/Chromium not found",
                       "Set browser.chrome_path in config"));
    }

    int debug_port = allocate_debug_port();
    auto instance_id = utils::generate_id(12);

    // Create a temporary user data directory
    auto user_data_dir = fs::temp_directory_path() / ("openclaw-chrome-" + instance_id);
    fs::create_directories(user_data_dir);

    // Fork and exec Chrome with remote debugging
    pid_t pid = ::fork();
    if (pid < 0) {
        return std::unexpected(
            make_error(ErrorCode::BrowserError,
                       "Failed to fork Chrome process",
                       "errno=" + std::to_string(errno)));
    }

    if (pid == 0) {
        // Child process: exec Chrome
        auto port_str = std::to_string(debug_port);
        auto debug_arg = "--remote-debugging-port=" + port_str;
        auto user_data_arg = "--user-data-dir=" + user_data_dir.string();

        ::execlp(chrome_path.c_str(), chrome_path.c_str(),
                 "--headless=new",
                 "--no-first-run",
                 "--no-default-browser-check",
                 "--disable-gpu",
                 "--disable-extensions",
                 "--disable-background-networking",
                 "--disable-sync",
                 "--disable-translate",
                 "--mute-audio",
                 "--no-sandbox",
                 debug_arg.c_str(),
                 user_data_arg.c_str(),
                 "about:blank",
                 nullptr);

        // If execlp returns, it failed
        ::_exit(127);
    }

    // Parent process: wait a bit for Chrome to start, then get the WS endpoint
    // Give Chrome a moment to start up
    ::usleep(500000);  // 500ms

    auto ws_endpoint = get_ws_endpoint(debug_port);
    if (!ws_endpoint) {
        ::kill(pid, SIGTERM);
        int status = 0;
        ::waitpid(pid, &status, WNOHANG);
        return std::unexpected(ws_endpoint.error());
    }

    auto instance = std::make_unique<BrowserInstance>();
    instance->id = instance_id;
    instance->cdp = std::make_unique<CdpClient>(impl_->ioc);
    instance->ws_endpoint = std::move(*ws_endpoint);
    instance->pid = pid;

    LOG_DEBUG("Chrome launched (pid={}, port={}, id={})", pid, debug_port,
              instance_id);
    return instance;
}

auto BrowserPool::find_chrome() const -> std::string {
    // Check config first
    if (impl_->config.chrome_path) {
        if (fs::exists(*impl_->config.chrome_path)) {
            return *impl_->config.chrome_path;
        }
    }

    // Well-known Chrome/Chromium paths
    static const std::vector<std::string> paths = {
#ifdef __APPLE__
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
        "/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary",
        "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
#endif
#ifdef __linux__
        "/usr/bin/google-chrome",
        "/usr/bin/google-chrome-stable",
        "/usr/bin/chromium",
        "/usr/bin/chromium-browser",
        "/snap/bin/chromium",
#endif
    };

    for (const auto& p : paths) {
        if (fs::exists(p)) {
            return p;
        }
    }

    // Try PATH
    if (const auto* path_env = std::getenv("PATH")) {
        auto dirs = utils::split(path_env, ':');
        for (const auto& dir : dirs) {
            for (const auto& name : {"google-chrome", "chromium", "chromium-browser"}) {
                auto full = fs::path(dir) / name;
                if (fs::exists(full)) {
                    return full.string();
                }
            }
        }
    }

    return {};
}

auto BrowserPool::get_ws_endpoint(int debug_port) -> Result<std::string> {
    // Chrome exposes /json/version at the debug port to get the WS URL
    try {
        // We use a simple synchronous HTTP request here since we need
        // the result before we can proceed. The Chrome /json/version
        // endpoint returns the DevTools WebSocket URL.
        //
        // In production, this could be made async, but since it's only
        // called during browser launch (which already has latency), the
        // synchronous approach is acceptable.

        // Try multiple times since Chrome might still be starting
        for (int attempt = 0; attempt < 10; ++attempt) {
            try {
                // Use httplib for a simple synchronous GET
                // We read from http://127.0.0.1:{debug_port}/json/version
                auto url = "http://127.0.0.1:" + std::to_string(debug_port);

                // Simple TCP connect + HTTP request
                net::io_context tmp_ioc;
                net::ip::tcp::socket sock(tmp_ioc);
                net::ip::tcp::resolver resolver(tmp_ioc);
                auto results = resolver.resolve("127.0.0.1",
                                                std::to_string(debug_port));
                net::connect(sock, results);

                std::string request = "GET /json/version HTTP/1.1\r\n"
                                      "Host: 127.0.0.1:" +
                                      std::to_string(debug_port) +
                                      "\r\n"
                                      "Connection: close\r\n\r\n";
                net::write(sock, net::buffer(request));

                std::string response;
                boost::system::error_code ec;
                char buf[4096];
                while (auto n = sock.read_some(net::buffer(buf), ec)) {
                    response.append(buf, n);
                }

                // Extract the JSON body (after the HTTP headers)
                auto body_pos = response.find("\r\n\r\n");
                if (body_pos == std::string::npos) {
                    ::usleep(200000);
                    continue;
                }

                auto body = response.substr(body_pos + 4);
                auto j = json::parse(body);

                if (j.contains("webSocketDebuggerUrl")) {
                    return j["webSocketDebuggerUrl"].get<std::string>();
                }

                ::usleep(200000);
            } catch (...) {
                ::usleep(200000);
            }
        }

        return std::unexpected(
            make_error(ErrorCode::BrowserError,
                       "Failed to get Chrome DevTools WebSocket URL",
                       "port=" + std::to_string(debug_port)));
    } catch (const std::exception& e) {
        return std::unexpected(
            make_error(ErrorCode::BrowserError,
                       "Failed to get Chrome WebSocket endpoint",
                       e.what()));
    }
}

auto BrowserPool::allocate_debug_port() -> int {
    return impl_->next_debug_port++;
}

} // namespace openclaw::browser
