#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/browser/cdp_client.hpp"
#include "openclaw/core/error.hpp"

namespace openclaw::browser {

using boost::asio::awaitable;
using json = nlohmann::json;

/// Options for navigation actions.
struct NavigateOptions {
    int timeout_ms = 30000;
    std::string wait_until = "load";  // "load", "domcontentloaded", "networkidle0"
};

/// Options for screenshot capture.
struct ScreenshotOptions {
    std::string format = "png";  // "png" or "jpeg"
    int quality = 80;            // JPEG quality (ignored for PNG)
    bool full_page = false;
    std::optional<json> clip;    // { x, y, width, height } region
};

/// Options for element interaction.
struct ClickOptions {
    std::string button = "left";  // "left", "right", "middle"
    int click_count = 1;
    int delay_ms = 0;
};

/// Options for typing text.
struct TypeOptions {
    int delay_ms = 0;  // delay between keystrokes in milliseconds
    bool clear_first = false;  // clear the field before typing
};

/// Options for waiting.
struct WaitOptions {
    int timeout_ms = 30000;
    int polling_ms = 100;
};

/// Result of evaluating JavaScript in the browser context.
struct EvalResult {
    json value;
    std::optional<std::string> exception;
};

/// An element found in the page via a CSS selector.
struct ElementInfo {
    std::string node_id;
    std::string tag_name;
    std::string text;
    json attributes;
    json bounding_box;  // { x, y, width, height }
};

/// High-level browser automation actions.
/// Wraps CDP commands into ergonomic methods for navigation, interaction, and extraction.
class BrowserAction {
public:
    explicit BrowserAction(CdpClient& cdp);
    ~BrowserAction() = default;

    // -- Navigation --

    /// Navigate to a URL and wait for the page to load.
    auto navigate(std::string_view url, const NavigateOptions& options = {})
        -> awaitable<Result<void>>;

    /// Go back in browser history.
    auto go_back() -> awaitable<Result<void>>;

    /// Go forward in browser history.
    auto go_forward() -> awaitable<Result<void>>;

    /// Reload the current page.
    auto reload() -> awaitable<Result<void>>;

    /// Get the current page URL.
    auto current_url() -> awaitable<Result<std::string>>;

    /// Get the current page title.
    auto title() -> awaitable<Result<std::string>>;

    // -- Interaction --

    /// Click on an element identified by a CSS selector.
    auto click(std::string_view selector, const ClickOptions& options = {})
        -> awaitable<Result<void>>;

    /// Type text into an element identified by a CSS selector.
    auto type(std::string_view selector, std::string_view text,
              const TypeOptions& options = {})
        -> awaitable<Result<void>>;

    /// Select an option in a <select> element by value.
    auto select(std::string_view selector, std::string_view value)
        -> awaitable<Result<void>>;

    /// v2026.2.26: Fill a form field with a value.
    /// Defaults `type` to "text" when not specified.
    auto fill(std::string_view selector, std::string_view value,
              std::string_view type = "text") -> awaitable<Result<void>>;

    /// Focus an element identified by a CSS selector.
    auto focus(std::string_view selector) -> awaitable<Result<void>>;

    /// Hover over an element identified by a CSS selector.
    auto hover(std::string_view selector) -> awaitable<Result<void>>;

    /// Press a keyboard key (e.g. "Enter", "Tab", "Escape").
    auto press_key(std::string_view key) -> awaitable<Result<void>>;

    /// Scroll the page by a given amount in pixels.
    auto scroll(int x, int y) -> awaitable<Result<void>>;

    // -- Extraction --

    /// Take a screenshot of the page.
    auto screenshot(const ScreenshotOptions& options = {})
        -> awaitable<Result<std::string>>;  // base64-encoded image data

    /// Get the text content of an element.
    auto get_text(std::string_view selector) -> awaitable<Result<std::string>>;

    /// Get the inner HTML of an element.
    auto get_html(std::string_view selector) -> awaitable<Result<std::string>>;

    /// Get an attribute value of an element.
    auto get_attribute(std::string_view selector, std::string_view attribute)
        -> awaitable<Result<std::string>>;

    /// Query for elements matching a CSS selector.
    auto query_all(std::string_view selector)
        -> awaitable<Result<std::vector<ElementInfo>>>;

    /// Get the full page HTML source.
    auto page_source() -> awaitable<Result<std::string>>;

    // -- JavaScript --

    /// Evaluate a JavaScript expression in the page context.
    auto evaluate(std::string_view expression)
        -> awaitable<Result<EvalResult>>;

    /// Execute JavaScript (no return value expected).
    auto execute(std::string_view script)
        -> awaitable<Result<void>>;

    // -- Waiting --

    /// Wait for an element matching the selector to appear in the DOM.
    auto wait_for(std::string_view selector, const WaitOptions& options = {})
        -> awaitable<Result<void>>;

    /// Wait for navigation to complete.
    auto wait_for_navigation(int timeout_ms = 30000)
        -> awaitable<Result<void>>;

    /// Wait for a fixed duration in milliseconds.
    auto wait(int ms) -> awaitable<void>;

private:
    auto resolve_selector(std::string_view selector) -> awaitable<Result<int>>;
    auto get_box_model(int node_id) -> awaitable<Result<json>>;
    auto dispatch_mouse_event(std::string_view type, double x, double y,
                              const ClickOptions& options = {})
        -> awaitable<Result<void>>;

    CdpClient& cdp_;
};

} // namespace openclaw::browser
