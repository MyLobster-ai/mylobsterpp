#include "openclaw/gateway/browser_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_browser_handlers(Protocol& protocol,
                               browser::BrowserPool& pool) {
    // browser.open
    protocol.register_method("browser.open",
        [&pool]([[maybe_unused]] json params) -> awaitable<json> {
            auto url = params.value("url", "about:blank");
            auto result = co_await pool.acquire();
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            auto* instance = result.value();
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

    // browser.navigate
    protocol.register_method("browser.navigate",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto url = params.value("url", "");
            if (url.empty()) {
                co_return json{{"ok", false}, {"error", "url is required"}};
            }
            // TODO: navigate via CDP command.
            co_return json{{"ok", true}, {"url", url}};
        },
        "Navigate to a URL", "browser");

    // browser.screenshot
    protocol.register_method("browser.screenshot",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // TODO: CDP Page.captureScreenshot.
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Take a screenshot", "browser");

    // browser.content
    protocol.register_method("browser.content",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // TODO: CDP Runtime.evaluate to get document content.
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Get page content as text/html", "browser");

    // browser.click
    protocol.register_method("browser.click",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Click an element on the page", "browser");

    // browser.type
    protocol.register_method("browser.type",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Type text into an input field", "browser");

    // browser.fill — v2026.2.26: fill a form field, defaults type to "text".
    protocol.register_method("browser.fill",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto selector = params.value("selector", "");
            auto value = params.value("value", "");
            if (selector.empty()) {
                co_return json{{"ok", false}, {"error", "selector is required"}};
            }
            // Default field type to "text" when not specified.
            auto type = params.value("type", "text");
            // TODO: wire to BrowserAction::fill via pool instance.
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Fill a form field with a value", "browser");

    // browser.evaluate
    protocol.register_method("browser.evaluate",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto expression = params.value("expression", "");
            if (expression.empty()) {
                co_return json{{"ok", false}, {"error", "expression is required"}};
            }
            // TODO: CDP Runtime.evaluate.
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Evaluate JavaScript on the page", "browser");

    // browser.wait
    protocol.register_method("browser.wait",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Wait for a selector or condition", "browser");

    // browser.scroll
    protocol.register_method("browser.scroll",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Scroll the page", "browser");

    // browser.pdf
    protocol.register_method("browser.pdf",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Export page as PDF", "browser");

    // browser.cookies.get
    protocol.register_method("browser.cookies.get",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Get browser cookies", "browser");

    // browser.cookies.set
    protocol.register_method("browser.cookies.set",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Set browser cookies", "browser");

    LOG_INFO("Registered browser handlers");
}

} // namespace openclaw::gateway
