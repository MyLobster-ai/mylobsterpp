#include "openclaw/browser/browser_pool.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"
#include "openclaw/infra/http_client.hpp"

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace openclaw::browser {

namespace fs = std::filesystem;
namespace net = boost::asio;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Platform helpers
// ---------------------------------------------------------------------------

static void platform_sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static void kill_browser_process(BrowserInstance& inst) {
#ifdef _WIN32
    if (inst.process_handle) {
        ::TerminateProcess(inst.process_handle, 1);
        ::WaitForSingleObject(inst.process_handle, 3000);
        ::CloseHandle(inst.process_handle);
        inst.process_handle = nullptr;
    }
#else
    if (inst.pid > 0) {
        ::kill(inst.pid, SIGTERM);
        int status = 0;
        ::waitpid(inst.pid, &status, WNOHANG);
    }
#endif
}

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
        kill_browser_process(*inst);
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
        kill_browser_process(*ptr);
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
    kill_browser_process(*inst);

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
        kill_browser_process(*inst);
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

#ifdef _WIN32
    // Windows: use CreateProcessA
    auto port_str = std::to_string(debug_port);
    auto cmd_line = "\"" + chrome_path + "\""
                    " --headless=new"
                    " --no-first-run"
                    " --no-default-browser-check"
                    " --disable-gpu"
                    " --disable-extensions"
                    " --disable-background-networking"
                    " --disable-sync"
                    " --disable-translate"
                    " --mute-audio"
                    " --no-sandbox"
                    " --remote-debugging-port=" + port_str +
                    " --user-data-dir=" + user_data_dir.string() +
                    " about:blank";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (!::CreateProcessA(nullptr, cmd_line.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return std::unexpected(
            make_error(ErrorCode::BrowserError,
                       "Failed to launch Chrome process",
                       "GetLastError=" + std::to_string(::GetLastError())));
    }

    ::CloseHandle(pi.hThread);

    // Wait for Chrome to start
    platform_sleep_ms(500);

    auto ws_endpoint = get_ws_endpoint(debug_port);
    if (!ws_endpoint) {
        ::TerminateProcess(pi.hProcess, 1);
        ::CloseHandle(pi.hProcess);
        return std::unexpected(ws_endpoint.error());
    }

    auto instance = std::make_unique<BrowserInstance>();
    instance->id = instance_id;
    instance->cdp = std::make_unique<CdpClient>(impl_->ioc);
    instance->ws_endpoint = std::move(*ws_endpoint);
    instance->pid = pi.dwProcessId;
    instance->process_handle = pi.hProcess;

    LOG_DEBUG("Chrome launched (pid={}, port={}, id={})", pi.dwProcessId,
              debug_port, instance_id);
    return instance;
#else
    // POSIX: fork and exec Chrome with remote debugging
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

    // Parent process: wait a bit for Chrome to start
    platform_sleep_ms(500);

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
#endif
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
#ifdef _WIN32
        "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
        "C:\\Program Files\\Chromium\\Application\\chrome.exe",
        "C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe",
#elif defined(__APPLE__)
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
        "/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary",
        "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
#elif defined(__linux__)
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
#ifdef _WIN32
        auto dirs = utils::split(path_env, ';');
        const std::vector<const char*> names = {"chrome.exe", "chromium.exe"};
#else
        auto dirs = utils::split(path_env, ':');
        const std::vector<const char*> names = {"google-chrome", "chromium", "chromium-browser"};
#endif
        for (const auto& dir : dirs) {
            for (const auto& name : names) {
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
        // Try multiple times since Chrome might still be starting
        for (int attempt = 0; attempt < 10; ++attempt) {
            try {
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
                    platform_sleep_ms(200);
                    continue;
                }

                auto body = response.substr(body_pos + 4);
                auto j = json::parse(body);

                if (j.contains("webSocketDebuggerUrl")) {
                    return j["webSocketDebuggerUrl"].get<std::string>();
                }

                platform_sleep_ms(200);
            } catch (...) {
                platform_sleep_ms(200);
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
