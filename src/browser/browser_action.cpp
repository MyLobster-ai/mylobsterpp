#include "openclaw/browser/browser_action.hpp"
#include "openclaw/core/logger.hpp"

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>

namespace openclaw::browser {

namespace net = boost::asio;

// ---------------------------------------------------------------------------
// BrowserAction
// ---------------------------------------------------------------------------

BrowserAction::BrowserAction(CdpClient& cdp) : cdp_(cdp) {}

// -- Navigation --

auto BrowserAction::navigate(std::string_view url, const NavigateOptions& options)
    -> awaitable<Result<void>> {
    auto result = co_await cdp_.send_command("Page.navigate", {
        {"url", std::string(url)},
    });
    if (!result) {
        co_return make_fail(result.error());
    }

    // Check for navigation error
    if (result->contains("errorText")) {
        co_return make_fail(
            make_error(ErrorCode::BrowserError,
                       "Navigation failed",
                       (*result)["errorText"].get<std::string>()));
    }

    // Wait for load event
    co_await wait_for_navigation(options.timeout_ms);
    LOG_DEBUG("Navigated to: {}", std::string(url));
    co_return ok_result();
}

auto BrowserAction::go_back() -> awaitable<Result<void>> {
    auto history = co_await cdp_.send_command("Page.getNavigationHistory");
    if (!history) {
        co_return make_fail(history.error());
    }

    int current_index = (*history)["currentIndex"].get<int>();
    if (current_index <= 0) {
        co_return make_fail(
            make_error(ErrorCode::BrowserError,
                       "Cannot go back: no previous history entry"));
    }

    auto& entries = (*history)["entries"];
    int target_id = entries[current_index - 1]["id"].get<int>();

    auto result = co_await cdp_.send_command("Page.navigateToHistoryEntry", {
        {"entryId", target_id},
    });
    if (!result) {
        co_return make_fail(result.error());
    }

    co_return ok_result();
}

auto BrowserAction::go_forward() -> awaitable<Result<void>> {
    auto history = co_await cdp_.send_command("Page.getNavigationHistory");
    if (!history) {
        co_return make_fail(history.error());
    }

    int current_index = (*history)["currentIndex"].get<int>();
    auto& entries = (*history)["entries"];
    if (current_index >= static_cast<int>(entries.size()) - 1) {
        co_return make_fail(
            make_error(ErrorCode::BrowserError,
                       "Cannot go forward: no next history entry"));
    }

    int target_id = entries[current_index + 1]["id"].get<int>();

    auto result = co_await cdp_.send_command("Page.navigateToHistoryEntry", {
        {"entryId", target_id},
    });
    if (!result) {
        co_return make_fail(result.error());
    }

    co_return ok_result();
}

auto BrowserAction::reload() -> awaitable<Result<void>> {
    auto result = co_await cdp_.send_command("Page.reload");
    if (!result) {
        co_return make_fail(result.error());
    }
    co_return ok_result();
}

auto BrowserAction::current_url() -> awaitable<Result<std::string>> {
    auto result = co_await evaluate("window.location.href");
    if (!result) {
        co_return make_fail(result.error());
    }
    if (result->value.is_string()) {
        co_return result->value.get<std::string>();
    }
    co_return std::string{};
}

auto BrowserAction::title() -> awaitable<Result<std::string>> {
    auto result = co_await evaluate("document.title");
    if (!result) {
        co_return make_fail(result.error());
    }
    if (result->value.is_string()) {
        co_return result->value.get<std::string>();
    }
    co_return std::string{};
}

// -- Interaction --

auto BrowserAction::click(std::string_view selector, const ClickOptions& options)
    -> awaitable<Result<void>> {
    auto node_id = co_await resolve_selector(selector);
    if (!node_id) {
        co_return make_fail(node_id.error());
    }

    auto box = co_await get_box_model(*node_id);
    if (!box) {
        co_return make_fail(box.error());
    }

    // Get the center of the content quad
    auto& content = (*box)["model"]["content"];
    double cx = (content[0].get<double>() + content[2].get<double>() +
                 content[4].get<double>() + content[6].get<double>()) / 4.0;
    double cy = (content[1].get<double>() + content[3].get<double>() +
                 content[5].get<double>() + content[7].get<double>()) / 4.0;

    // Move mouse to element center
    auto move_result = co_await dispatch_mouse_event("mouseMoved", cx, cy);
    if (!move_result) {
        co_return make_fail(move_result.error());
    }

    // Click
    for (int i = 0; i < options.click_count; ++i) {
        auto press = co_await dispatch_mouse_event("mousePressed", cx, cy, options);
        if (!press) {
            co_return make_fail(press.error());
        }

        if (options.delay_ms > 0) {
            co_await wait(options.delay_ms);
        }

        auto release = co_await dispatch_mouse_event("mouseReleased", cx, cy, options);
        if (!release) {
            co_return make_fail(release.error());
        }
    }

    LOG_DEBUG("Clicked: {}", std::string(selector));
    co_return ok_result();
}

auto BrowserAction::type(std::string_view selector, std::string_view text,
                         const TypeOptions& options) -> awaitable<Result<void>> {
    // Focus the element first
    auto focus_result = co_await focus(selector);
    if (!focus_result) {
        co_return make_fail(focus_result.error());
    }

    // Optionally clear existing content
    if (options.clear_first) {
        co_await cdp_.send_command("Input.dispatchKeyEvent", {
            {"type", "rawKeyDown"},
            {"key", "a"},
            {"code", "KeyA"},
            {"windowsVirtualKeyCode", 65},
            {"nativeVirtualKeyCode", 65},
            {"modifiers", 2},  // Ctrl/Cmd
        });
        co_await cdp_.send_command("Input.dispatchKeyEvent", {
            {"type", "keyUp"},
            {"key", "a"},
            {"code", "KeyA"},
            {"windowsVirtualKeyCode", 65},
            {"modifiers", 2},
        });
        co_await cdp_.send_command("Input.dispatchKeyEvent", {
            {"type", "rawKeyDown"},
            {"key", "Backspace"},
            {"code", "Backspace"},
            {"windowsVirtualKeyCode", 8},
            {"nativeVirtualKeyCode", 8},
        });
        co_await cdp_.send_command("Input.dispatchKeyEvent", {
            {"type", "keyUp"},
            {"key", "Backspace"},
            {"code", "Backspace"},
            {"windowsVirtualKeyCode", 8},
        });
    }

    // Type each character
    for (char ch : text) {
        auto result = co_await cdp_.send_command("Input.dispatchKeyEvent", {
            {"type", "char"},
            {"text", std::string(1, ch)},
        });
        if (!result) {
            co_return make_fail(result.error());
        }

        if (options.delay_ms > 0) {
            co_await wait(options.delay_ms);
        }
    }

    LOG_DEBUG("Typed into {}: {} chars", std::string(selector), text.size());
    co_return ok_result();
}

auto BrowserAction::select(std::string_view selector, std::string_view value)
    -> awaitable<Result<void>> {
    auto js = "(() => { const el = document.querySelector('" +
              std::string(selector) + "'); if (!el) return false; "
              "el.value = '" + std::string(value) + "'; "
              "el.dispatchEvent(new Event('change', { bubbles: true })); "
              "return true; })()";
    auto result = co_await evaluate(js);
    if (!result) {
        co_return make_fail(result.error());
    }
    if (result->value.is_boolean() && !result->value.get<bool>()) {
        co_return make_fail(
            make_error(ErrorCode::BrowserError,
                       "Select element not found",
                       std::string(selector)));
    }
    co_return ok_result();
}

auto BrowserAction::focus(std::string_view selector) -> awaitable<Result<void>> {
    auto node_id = co_await resolve_selector(selector);
    if (!node_id) {
        co_return make_fail(node_id.error());
    }

    auto result = co_await cdp_.send_command("DOM.focus", {
        {"nodeId", *node_id},
    });
    if (!result) {
        co_return make_fail(result.error());
    }
    co_return ok_result();
}

auto BrowserAction::hover(std::string_view selector) -> awaitable<Result<void>> {
    auto node_id = co_await resolve_selector(selector);
    if (!node_id) {
        co_return make_fail(node_id.error());
    }

    auto box = co_await get_box_model(*node_id);
    if (!box) {
        co_return make_fail(box.error());
    }

    auto& content = (*box)["model"]["content"];
    double cx = (content[0].get<double>() + content[2].get<double>() +
                 content[4].get<double>() + content[6].get<double>()) / 4.0;
    double cy = (content[1].get<double>() + content[3].get<double>() +
                 content[5].get<double>() + content[7].get<double>()) / 4.0;

    co_return co_await dispatch_mouse_event("mouseMoved", cx, cy);
}

auto BrowserAction::press_key(std::string_view key) -> awaitable<Result<void>> {
    auto down = co_await cdp_.send_command("Input.dispatchKeyEvent", {
        {"type", "rawKeyDown"},
        {"key", std::string(key)},
    });
    if (!down) {
        co_return make_fail(down.error());
    }

    auto up = co_await cdp_.send_command("Input.dispatchKeyEvent", {
        {"type", "keyUp"},
        {"key", std::string(key)},
    });
    if (!up) {
        co_return make_fail(up.error());
    }

    co_return ok_result();
}

auto BrowserAction::scroll(int x, int y) -> awaitable<Result<void>> {
    auto result = co_await cdp_.send_command("Input.dispatchMouseEvent", {
        {"type", "mouseWheel"},
        {"x", 0},
        {"y", 0},
        {"deltaX", x},
        {"deltaY", y},
    });
    if (!result) {
        co_return make_fail(result.error());
    }
    co_return ok_result();
}

// -- Extraction --

auto BrowserAction::screenshot(const ScreenshotOptions& options)
    -> awaitable<Result<std::string>> {
    json params = {
        {"format", options.format},
    };
    if (options.format == "jpeg") {
        params["quality"] = options.quality;
    }
    if (options.full_page) {
        // Get layout metrics for full-page capture
        auto metrics = co_await cdp_.send_command(
            "Page.getLayoutMetrics");
        if (metrics) {
            auto& content_size = (*metrics)["cssContentSize"];
            params["clip"] = {
                {"x", 0},
                {"y", 0},
                {"width", content_size["width"]},
                {"height", content_size["height"]},
                {"scale", 1},
            };
            params["captureBeyondViewport"] = true;
        }
    } else if (options.clip) {
        params["clip"] = *options.clip;
    }

    auto result = co_await cdp_.send_command("Page.captureScreenshot", params);
    if (!result) {
        co_return make_fail(result.error());
    }

    co_return (*result)["data"].get<std::string>();
}

auto BrowserAction::get_text(std::string_view selector)
    -> awaitable<Result<std::string>> {
    auto js = "document.querySelector('" + std::string(selector) +
              "')?.textContent || ''";
    auto result = co_await evaluate(js);
    if (!result) {
        co_return make_fail(result.error());
    }
    if (result->value.is_string()) {
        co_return result->value.get<std::string>();
    }
    co_return std::string{};
}

auto BrowserAction::get_html(std::string_view selector)
    -> awaitable<Result<std::string>> {
    auto js = "document.querySelector('" + std::string(selector) +
              "')?.innerHTML || ''";
    auto result = co_await evaluate(js);
    if (!result) {
        co_return make_fail(result.error());
    }
    if (result->value.is_string()) {
        co_return result->value.get<std::string>();
    }
    co_return std::string{};
}

auto BrowserAction::get_attribute(std::string_view selector,
                                  std::string_view attribute)
    -> awaitable<Result<std::string>> {
    auto js = "document.querySelector('" + std::string(selector) +
              "')?.getAttribute('" + std::string(attribute) + "') || ''";
    auto result = co_await evaluate(js);
    if (!result) {
        co_return make_fail(result.error());
    }
    if (result->value.is_string()) {
        co_return result->value.get<std::string>();
    }
    co_return std::string{};
}

auto BrowserAction::query_all(std::string_view selector)
    -> awaitable<Result<std::vector<ElementInfo>>> {
    auto js = R"JS(
        (() => {
            const els = document.querySelectorAll(')JS" + std::string(selector) + R"JS(');
            return Array.from(els).map(el => {
                const rect = el.getBoundingClientRect();
                const attrs = {};
                for (const attr of el.attributes) {
                    attrs[attr.name] = attr.value;
                }
                return {
                    tag_name: el.tagName.toLowerCase(),
                    text: el.textContent?.trim()?.substring(0, 200) || '',
                    attributes: attrs,
                    bounding_box: {
                        x: rect.x, y: rect.y,
                        width: rect.width, height: rect.height
                    }
                };
            });
        })()
    )JS";

    auto result = co_await evaluate(js);
    if (!result) {
        co_return make_fail(result.error());
    }

    std::vector<ElementInfo> elements;
    if (result->value.is_array()) {
        for (const auto& item : result->value) {
            ElementInfo info;
            info.tag_name = item.value("tag_name", "");
            info.text = item.value("text", "");
            info.attributes = item.value("attributes", json::object());
            info.bounding_box = item.value("bounding_box", json::object());
            elements.push_back(std::move(info));
        }
    }

    co_return elements;
}

auto BrowserAction::page_source() -> awaitable<Result<std::string>> {
    auto result = co_await evaluate("document.documentElement.outerHTML");
    if (!result) {
        co_return make_fail(result.error());
    }
    if (result->value.is_string()) {
        co_return result->value.get<std::string>();
    }
    co_return std::string{};
}

// -- JavaScript --

auto BrowserAction::evaluate(std::string_view expression)
    -> awaitable<Result<EvalResult>> {
    auto result = co_await cdp_.send_command("Runtime.evaluate", {
        {"expression", std::string(expression)},
        {"returnByValue", true},
        {"awaitPromise", true},
    });
    if (!result) {
        co_return make_fail(result.error());
    }

    EvalResult eval_result;

    if (result->contains("exceptionDetails")) {
        auto& exc = (*result)["exceptionDetails"];
        eval_result.exception = exc.value("text", "Unknown exception");
        if (exc.contains("exception") && exc["exception"].contains("description")) {
            eval_result.exception = exc["exception"]["description"].get<std::string>();
        }
    }

    if (result->contains("result")) {
        auto& remote_obj = (*result)["result"];
        if (remote_obj.contains("value")) {
            eval_result.value = remote_obj["value"];
        } else if (remote_obj.contains("description")) {
            eval_result.value = remote_obj["description"];
        }
    }

    co_return eval_result;
}

auto BrowserAction::execute(std::string_view script) -> awaitable<Result<void>> {
    auto result = co_await evaluate(script);
    if (!result) {
        co_return make_fail(result.error());
    }
    if (result->exception) {
        co_return make_fail(
            make_error(ErrorCode::BrowserError,
                       "Script execution failed",
                       *result->exception));
    }
    co_return ok_result();
}

// -- Waiting --

auto BrowserAction::wait_for(std::string_view selector,
                              const WaitOptions& options) -> awaitable<Result<void>> {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(options.timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        auto js = "!!document.querySelector('" + std::string(selector) + "')";
        auto result = co_await evaluate(js);
        if (result && result->value.is_boolean() && result->value.get<bool>()) {
            co_return ok_result();
        }

        co_await wait(options.polling_ms);
    }

    co_return make_fail(
        make_error(ErrorCode::Timeout,
                   "Timeout waiting for selector",
                   std::string(selector)));
}

auto BrowserAction::wait_for_navigation(int timeout_ms)
    -> awaitable<Result<void>> {
    // We wait for the Page.loadEventFired event by polling the readyState
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        auto result = co_await evaluate("document.readyState");
        if (result && result->value.is_string()) {
            auto state = result->value.get<std::string>();
            if (state == "complete" || state == "interactive") {
                co_return ok_result();
            }
        }
        co_await wait(100);
    }

    co_return make_fail(
        make_error(ErrorCode::Timeout,
                   "Navigation timeout",
                   std::to_string(timeout_ms) + "ms"));
}

auto BrowserAction::wait(int ms) -> awaitable<void> {
    net::steady_timer timer(co_await net::this_coro::executor);
    timer.expires_after(std::chrono::milliseconds(ms));
    co_await timer.async_wait(net::use_awaitable);
}

// -- Private helpers --

auto BrowserAction::resolve_selector(std::string_view selector)
    -> awaitable<Result<int>> {
    // Get the document root node
    auto doc = co_await cdp_.send_command("DOM.getDocument", {
        {"depth", 0},
    });
    if (!doc) {
        co_return make_fail(doc.error());
    }

    int root_node_id = (*doc)["root"]["nodeId"].get<int>();

    // Query for the element
    auto query = co_await cdp_.send_command("DOM.querySelector", {
        {"nodeId", root_node_id},
        {"selector", std::string(selector)},
    });
    if (!query) {
        co_return make_fail(query.error());
    }

    int node_id = (*query)["nodeId"].get<int>();
    if (node_id == 0) {
        co_return make_fail(
            make_error(ErrorCode::NotFound,
                       "Element not found",
                       std::string(selector)));
    }

    co_return node_id;
}

auto BrowserAction::get_box_model(int node_id) -> awaitable<Result<json>> {
    auto result = co_await cdp_.send_command("DOM.getBoxModel", {
        {"nodeId", node_id},
    });
    if (!result) {
        co_return make_fail(result.error());
    }
    co_return *result;
}

auto BrowserAction::dispatch_mouse_event(std::string_view type, double x,
                                         double y,
                                         const ClickOptions& options)
    -> awaitable<Result<void>> {
    json params = {
        {"type", std::string(type)},
        {"x", x},
        {"y", y},
        {"button", options.button},
        {"clickCount", options.click_count},
    };

    auto result = co_await cdp_.send_command("Input.dispatchMouseEvent", params);
    if (!result) {
        co_return make_fail(result.error());
    }
    co_return ok_result();
}

} // namespace openclaw::browser
