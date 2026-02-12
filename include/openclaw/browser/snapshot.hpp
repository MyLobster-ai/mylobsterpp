#pragma once

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

/// A node in the accessibility tree.
struct AccessibilityNode {
    std::string role;
    std::string name;
    std::string value;
    std::vector<AccessibilityNode> children;
    json properties;  // additional ARIA properties
    bool ignored = false;
};

void to_json(json& j, const AccessibilityNode& n);
void from_json(const json& j, AccessibilityNode& n);

/// A node in the DOM snapshot.
struct DomNode {
    int node_id = 0;
    std::string node_type;  // "element", "text", "document", etc.
    std::string tag_name;
    std::string text_content;
    json attributes;
    json bounding_box;  // { x, y, width, height }
    std::vector<DomNode> children;
};

void to_json(json& j, const DomNode& n);
void from_json(const json& j, DomNode& n);

/// Options for snapshot capture.
struct SnapshotOptions {
    bool include_whitespace = false;
    bool include_hidden = false;
    int max_depth = -1;  // -1 for unlimited depth
};

/// Captures DOM snapshots and accessibility trees from a browser page.
/// Useful for extracting structured content and for LLM-readable page representations.
class Snapshot {
public:
    explicit Snapshot(CdpClient& cdp);
    ~Snapshot() = default;

    /// Capture the full DOM tree of the current page.
    auto capture_dom(const SnapshotOptions& options = {})
        -> awaitable<Result<DomNode>>;

    /// Capture the accessibility tree of the current page.
    auto capture_accessibility_tree()
        -> awaitable<Result<AccessibilityNode>>;

    /// Extract all visible text content from the page.
    auto extract_text() -> awaitable<Result<std::string>>;

    /// Extract text content from a specific element and its descendants.
    auto extract_text(std::string_view selector)
        -> awaitable<Result<std::string>>;

    /// Get a simplified, LLM-friendly text representation of the page.
    /// Combines accessibility tree roles, names, and text into a readable format.
    auto to_text_representation(const SnapshotOptions& options = {})
        -> awaitable<Result<std::string>>;

    /// Get all links on the page as (text, href) pairs.
    auto extract_links()
        -> awaitable<Result<std::vector<std::pair<std::string, std::string>>>>;

    /// Get all form fields on the page.
    auto extract_form_fields()
        -> awaitable<Result<json>>;

    /// Capture a serialized snapshot (DOM + accessibility) as JSON.
    auto capture_full(const SnapshotOptions& options = {})
        -> awaitable<Result<json>>;

private:
    auto build_dom_tree(const json& nodes, const json& strings, int index,
                        int depth, const SnapshotOptions& options)
        -> DomNode;
    auto build_accessibility_tree(const json& node) -> AccessibilityNode;
    auto flatten_text(const DomNode& node) -> std::string;
    auto render_accessibility_node(const AccessibilityNode& node, int indent)
        -> std::string;

    CdpClient& cdp_;
};

} // namespace openclaw::browser
