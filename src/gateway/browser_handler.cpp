#include "openclaw/gateway/browser_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/browser/browser_action.hpp"
#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

/// Helper: acquire an instance from the pool and return a BrowserAction.
/// The caller is responsible for releasing the instance when done.
static auto get_instance_action(browser::BrowserPool& pool,
                                const std::string& instance_id)
    -> awaitable<Result<std::pair<browser::BrowserInstance*, browser::BrowserAction>>> {
    // For now, we re-acquire from the pool by ID lookup.
    // The pool holds instances by ID, so acquire gets an existing or new one.
    auto result = co_await pool.acquire();
    if (!result.has_value()) {
        co_return make_fail(result.error());
    }
    auto* instance = result.value();
    if (!instance->cdp || !instance->cdp->is_connected()) {
        co_return make_fail(
            make_error(ErrorCode::ConnectionClosed, "CDP client not connected"));
    }
    browser::BrowserAction action(*instance->cdp);
    co_return std::make_pair(instance, std::move(action));
}

void register_browser_handlers(Protocol& protocol,
                               browser::BrowserPool& pool) {
    // browser.open — acquire an instance and optionally navigate to a URL.
    protocol.register_method("browser.open",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto url = params.value("url", "about:blank");
            auto result = co_await pool.acquire();
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            auto* instance = result.value();

            // Navigate to the requested URL if not about:blank.
            if (url != "about:blank" && instance->cdp && instance->cdp->is_connected()) {
                browser::BrowserAction action(*instance->cdp);
                auto nav = co_await action.navigate(url);
                if (!nav) {
                    LOG_WARN("browser.open: navigate to {} failed: {}", url, nav.error().what());
                }
            }

            co_return json{
                {"ok", true},
                {"instanceId", instance->id},
                {"url", url},
            };
        },
        "Open a URL in headless browser", "browser");

    // browser.close
    protocol.register_method("browser.close",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("instanceId", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "instanceId is required"}};
            }
            auto result = co_await pool.close(id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Close a browser page", "browser");

    // browser.navigate — navigate to a URL via CDP.
    protocol.register_method("browser.navigate",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto url = params.value("url", "");
            if (url.empty()) {
                co_return json{{"ok", false}, {"error", "url is required"}};
            }
            auto inst = co_await get_instance_action(pool, params.value("instanceId", ""));
            if (!inst) {
                co_return json{{"ok", false}, {"error", inst.error().what()}};
            }
            auto& [instance, action] = *inst;
            browser::NavigateOptions opts;
            opts.timeout_ms = params.value("timeout", 30000);
            opts.wait_until = params.value("waitUntil", "load");
            auto result = co_await action.navigate(url, opts);
            pool.release(instance);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            auto current = co_await action.current_url();
            co_return json{
                {"ok", true},
                {"url", current ? *current : url},
            };
        },
        "Navigate to a URL", "browser");

    // browser.screenshot — capture page screenshot via CDP.
    protocol.register_method("browser.screenshot",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto inst = co_await get_instance_action(pool, params.value("instanceId", ""));
            if (!inst) {
                co_return json{{"ok", false}, {"error", inst.error().what()}};
            }
            auto& [instance, action] = *inst;
            browser::ScreenshotOptions opts;
            opts.format = params.value("format", "png");
            opts.quality = params.value("quality", 80);
            opts.full_page = params.value("fullPage", false);
            if (params.contains("clip")) {
                opts.clip = params["clip"];
            }
            auto result = co_await action.screenshot(opts);
            pool.release(instance);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{
                {"ok", true},
                {"data", *result},
                {"format", opts.format},
            };
        },
        "Take a screenshot", "browser");

    // browser.content — get page source HTML/text via CDP.
    protocol.register_method("browser.content",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto inst = co_await get_instance_action(pool, params.value("instanceId", ""));
            if (!inst) {
                co_return json{{"ok", false}, {"error", inst.error().what()}};
            }
            auto& [instance, action] = *inst;
            auto mode = params.value("mode", "html");
            Result<std::string> result;
            if (mode == "text") {
                auto selector = params.value("selector", "body");
                result = co_await action.get_text(selector);
            } else {
                result = co_await action.page_source();
            }
            pool.release(instance);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}, {"content", *result}, {"mode", mode}};
        },
        "Get page content as text/html", "browser");

    // browser.click — click an element by CSS selector.
    protocol.register_method("browser.click",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto selector = params.value("selector", "");
            if (selector.empty()) {
                co_return json{{"ok", false}, {"error", "selector is required"}};
            }
            auto inst = co_await get_instance_action(pool, params.value("instanceId", ""));
            if (!inst) {
                co_return json{{"ok", false}, {"error", inst.error().what()}};
            }
            auto& [instance, action] = *inst;
            browser::ClickOptions opts;
            opts.button = params.value("button", "left");
            opts.click_count = params.value("clickCount", 1);
            opts.delay_ms = params.value("delay", 0);
            auto result = co_await action.click(selector, opts);
            pool.release(instance);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Click an element on the page", "browser");

    // browser.type — type text into an element.
    protocol.register_method("browser.type",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto selector = params.value("selector", "");
            auto text = params.value("text", "");
            if (selector.empty() || text.empty()) {
                co_return json{{"ok", false}, {"error", "selector and text are required"}};
            }
            auto inst = co_await get_instance_action(pool, params.value("instanceId", ""));
            if (!inst) {
                co_return json{{"ok", false}, {"error", inst.error().what()}};
            }
            auto& [instance, action] = *inst;
            browser::TypeOptions opts;
            opts.delay_ms = params.value("delay", 0);
            opts.clear_first = params.value("clearFirst", false);
            auto result = co_await action.type(selector, text, opts);
            pool.release(instance);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Type text into an input field", "browser");

    // browser.fill — v2026.2.26: fill a form field, defaults type to "text".
    protocol.register_method("browser.fill",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto selector = params.value("selector", "");
            auto value = params.value("value", "");
            if (selector.empty()) {
                co_return json{{"ok", false}, {"error", "selector is required"}};
            }
            auto type = params.value("type", "text");
            auto inst = co_await get_instance_action(pool, params.value("instanceId", ""));
            if (!inst) {
                co_return json{{"ok", false}, {"error", inst.error().what()}};
            }
            auto& [instance, action] = *inst;
            auto result = co_await action.fill(selector, value, type);
            pool.release(instance);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Fill a form field with a value", "browser");

    // browser.evaluate — execute JavaScript in the page context.
    protocol.register_method("browser.evaluate",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto expression = params.value("expression", "");
            if (expression.empty()) {
                co_return json{{"ok", false}, {"error", "expression is required"}};
            }
            auto inst = co_await get_instance_action(pool, params.value("instanceId", ""));
            if (!inst) {
                co_return json{{"ok", false}, {"error", inst.error().what()}};
            }
            auto& [instance, action] = *inst;
            auto result = co_await action.evaluate(expression);
            pool.release(instance);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            json response = {{"ok", true}, {"result", result->value}};
            if (result->exception) {
                response["exception"] = *result->exception;
            }
            co_return response;
        },
        "Evaluate JavaScript on the page", "browser");

    // browser.wait — wait for a selector or fixed duration.
    protocol.register_method("browser.wait",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto inst = co_await get_instance_action(pool, params.value("instanceId", ""));
            if (!inst) {
                co_return json{{"ok", false}, {"error", inst.error().what()}};
            }
            auto& [instance, action] = *inst;

            auto selector = params.value("selector", "");
            if (!selector.empty()) {
                browser::WaitOptions opts;
                opts.timeout_ms = params.value("timeout", 30000);
                opts.polling_ms = params.value("polling", 100);
                auto result = co_await action.wait_for(selector, opts);
                pool.release(instance);
                if (!result) {
                    co_return json{{"ok", false}, {"error", result.error().what()}};
                }
            } else {
                int ms = params.value("ms", 1000);
                co_await action.wait(ms);
                pool.release(instance);
            }
            co_return json{{"ok", true}};
        },
        "Wait for a selector or condition", "browser");

    // browser.scroll — scroll the page by pixel offsets.
    protocol.register_method("browser.scroll",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto inst = co_await get_instance_action(pool, params.value("instanceId", ""));
            if (!inst) {
                co_return json{{"ok", false}, {"error", inst.error().what()}};
            }
            auto& [instance, action] = *inst;
            int x = params.value("x", 0);
            int y = params.value("y", 0);
            auto result = co_await action.scroll(x, y);
            pool.release(instance);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Scroll the page", "browser");

    // browser.pdf — export page as PDF via CDP.
    protocol.register_method("browser.pdf",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto inst = co_await get_instance_action(pool, params.value("instanceId", ""));
            if (!inst) {
                co_return json{{"ok", false}, {"error", inst.error().what()}};
            }
            auto& [instance, action] = *inst;
            // Use CDP Page.printToPDF directly.
            json pdf_params;
            pdf_params["landscape"] = params.value("landscape", false);
            pdf_params["printBackground"] = params.value("printBackground", true);
            pdf_params["preferCSSPageSize"] = params.value("preferCSSPageSize", false);
            auto result = co_await instance->cdp->send_command("Page.printToPDF", pdf_params);
            pool.release(instance);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{
                {"ok", true},
                {"data", (*result).value("data", "")},
            };
        },
        "Export page as PDF", "browser");

    // browser.cookies.get — get cookies via CDP.
    protocol.register_method("browser.cookies.get",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto inst = co_await get_instance_action(pool, params.value("instanceId", ""));
            if (!inst) {
                co_return json{{"ok", false}, {"error", inst.error().what()}};
            }
            auto& [instance, action] = *inst;
            json cmd_params;
            if (params.contains("urls")) {
                cmd_params["urls"] = params["urls"];
            }
            auto result = co_await instance->cdp->send_command("Network.getCookies", cmd_params);
            pool.release(instance);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}, {"cookies", (*result).value("cookies", json::array())}};
        },
        "Get browser cookies", "browser");

    // browser.cookies.set — set cookies via CDP.
    protocol.register_method("browser.cookies.set",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto inst = co_await get_instance_action(pool, params.value("instanceId", ""));
            if (!inst) {
                co_return json{{"ok", false}, {"error", inst.error().what()}};
            }
            auto& [instance, action] = *inst;
            if (!params.contains("cookies") || !params["cookies"].is_array()) {
                pool.release(instance);
                co_return json{{"ok", false}, {"error", "cookies array is required"}};
            }
            for (const auto& cookie : params["cookies"]) {
                auto result = co_await instance->cdp->send_command("Network.setCookie", cookie);
                if (!result) {
                    pool.release(instance);
                    co_return json{{"ok", false}, {"error", result.error().what()}};
                }
            }
            pool.release(instance);
            co_return json{{"ok", true}};
        },
        "Set browser cookies", "browser");

    LOG_INFO("Registered browser handlers (CDP-wired)");
}

} // namespace openclaw::gateway
