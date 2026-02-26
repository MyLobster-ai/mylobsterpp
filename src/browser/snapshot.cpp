#include "openclaw/browser/snapshot.hpp"
#include "openclaw/core/logger.hpp"

#include <sstream>

namespace openclaw::browser {

// ---------------------------------------------------------------------------
// JSON serialization
// ---------------------------------------------------------------------------

void to_json(json& j, const AccessibilityNode& n) {
    j = json{
        {"role", n.role},
        {"name", n.name},
        {"value", n.value},
        {"properties", n.properties},
        {"ignored", n.ignored},
    };
    if (!n.children.empty()) {
        j["children"] = n.children;
    }
}

void from_json(const json& j, AccessibilityNode& n) {
    n.role = j.value("role", "");
    n.name = j.value("name", "");
    n.value = j.value("value", "");
    n.properties = j.value("properties", json::object());
    n.ignored = j.value("ignored", false);
    if (j.contains("children")) {
        n.children = j["children"].get<std::vector<AccessibilityNode>>();
    }
}

void to_json(json& j, const DomNode& n) {
    j = json{
        {"node_id", n.node_id},
        {"node_type", n.node_type},
        {"tag_name", n.tag_name},
        {"text_content", n.text_content},
        {"attributes", n.attributes},
        {"bounding_box", n.bounding_box},
    };
    if (!n.children.empty()) {
        j["children"] = n.children;
    }
}

void from_json(const json& j, DomNode& n) {
    n.node_id = j.value("node_id", 0);
    n.node_type = j.value("node_type", "");
    n.tag_name = j.value("tag_name", "");
    n.text_content = j.value("text_content", "");
    n.attributes = j.value("attributes", json::object());
    n.bounding_box = j.value("bounding_box", json::object());
    if (j.contains("children")) {
        n.children = j["children"].get<std::vector<DomNode>>();
    }
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------

Snapshot::Snapshot(CdpClient& cdp) : cdp_(cdp) {}

auto Snapshot::capture_dom(const SnapshotOptions& options)
    -> awaitable<Result<DomNode>> {
    // Use DOMSnapshot.captureSnapshot for a comprehensive view
    auto result = co_await cdp_.send_command("DOMSnapshot.captureSnapshot", {
        {"computedStyles", json::array()},
        {"includeDOMRects", true},
        {"includePaintOrder", false},
    });

    if (!result) {
        // Fall back to DOM.getDocument for simpler tree
        auto doc = co_await cdp_.send_command("DOM.getDocument", {
            {"depth", options.max_depth >= 0 ? options.max_depth : -1},
            {"pierce", true},
        });
        if (!doc) {
            co_return make_fail(doc.error());
        }

        // Convert CDP document node to our DomNode
        auto& root = (*doc)["root"];
        DomNode node;
        node.node_id = root.value("nodeId", 0);
        node.node_type = "document";
        node.tag_name = root.value("nodeName", "");
        co_return node;
    }

    // Parse the DOMSnapshot format
    if (!result->contains("documents") || (*result)["documents"].empty()) {
        co_return make_fail(
            make_error(ErrorCode::BrowserError,
                       "DOM snapshot returned no documents"));
    }

    auto& doc = (*result)["documents"][0];
    auto& nodes = doc["nodes"];
    auto& strings = (*result)["strings"];

    // Build tree starting from root (index 0)
    auto root = build_dom_tree(nodes, strings, 0, 0, options);

    LOG_DEBUG("Captured DOM snapshot with {} string table entries",
              strings.size());
    co_return root;
}

auto Snapshot::capture_accessibility_tree()
    -> awaitable<Result<AccessibilityNode>> {
    auto result = co_await cdp_.send_command(
        "Accessibility.getFullAXTree");
    if (!result) {
        co_return make_fail(result.error());
    }

    if (!result->contains("nodes") || (*result)["nodes"].empty()) {
        co_return make_fail(
            make_error(ErrorCode::BrowserError,
                       "Accessibility tree is empty"));
    }

    // Build a lookup map: nodeId -> json node
    auto& nodes = (*result)["nodes"];
    std::unordered_map<std::string, json> node_map;
    for (const auto& node : nodes) {
        auto id = node["nodeId"].get<std::string>();
        node_map[id] = node;
    }

    // Find the root node (first one or the one with role "RootWebArea")
    json root_json;
    for (const auto& node : nodes) {
        if (node.contains("role") && node["role"].contains("value")) {
            auto role_val = node["role"]["value"].get<std::string>();
            if (role_val == "RootWebArea" || role_val == "WebArea") {
                root_json = node;
                break;
            }
        }
    }
    if (root_json.is_null()) {
        root_json = nodes[0];
    }

    auto root = build_accessibility_tree(root_json);

    // Recursively attach children from the node_map
    std::function<void(AccessibilityNode&, const json&)> attach_children;
    attach_children = [&](AccessibilityNode& parent, const json& src) {
        if (!src.contains("childIds")) return;
        for (const auto& child_id : src["childIds"]) {
            auto cid = child_id.get<std::string>();
            auto it = node_map.find(cid);
            if (it != node_map.end()) {
                auto child = build_accessibility_tree(it->second);
                attach_children(child, it->second);
                if (!child.ignored || !child.children.empty()) {
                    parent.children.push_back(std::move(child));
                }
            }
        }
    };
    attach_children(root, root_json);

    LOG_DEBUG("Captured accessibility tree with {} total nodes", nodes.size());
    co_return root;
}

auto Snapshot::extract_text() -> awaitable<Result<std::string>> {
    auto result = co_await cdp_.send_command("Runtime.evaluate", {
        {"expression", "document.body?.innerText || ''"},
        {"returnByValue", true},
    });
    if (!result) {
        co_return make_fail(result.error());
    }

    if (result->contains("result") && (*result)["result"].contains("value")) {
        co_return (*result)["result"]["value"].get<std::string>();
    }
    co_return std::string{};
}

auto Snapshot::extract_text(std::string_view selector)
    -> awaitable<Result<std::string>> {
    auto js = "document.querySelector('" + std::string(selector) +
              "')?.innerText || ''";
    auto result = co_await cdp_.send_command("Runtime.evaluate", {
        {"expression", js},
        {"returnByValue", true},
    });
    if (!result) {
        co_return make_fail(result.error());
    }

    if (result->contains("result") && (*result)["result"].contains("value")) {
        co_return (*result)["result"]["value"].get<std::string>();
    }
    co_return std::string{};
}

auto Snapshot::to_text_representation(const SnapshotOptions& options)
    -> awaitable<Result<std::string>> {
    // Try accessibility tree first (most LLM-friendly)
    auto a11y = co_await capture_accessibility_tree();
    if (a11y) {
        auto text = render_accessibility_node(*a11y, 0);
        co_return text;
    }

    // Fall back to DOM text extraction
    auto dom_text = co_await extract_text();
    if (!dom_text) {
        co_return make_fail(dom_text.error());
    }
    co_return *dom_text;
}

auto Snapshot::extract_links()
    -> awaitable<Result<std::vector<std::pair<std::string, std::string>>>> {
    auto js = R"JS(
        (() => {
            const links = document.querySelectorAll('a[href]');
            return Array.from(links).map(a => ({
                text: a.textContent?.trim()?.substring(0, 200) || '',
                href: a.href || ''
            }));
        })()
    )JS";

    auto result = co_await cdp_.send_command("Runtime.evaluate", {
        {"expression", js},
        {"returnByValue", true},
        {"awaitPromise", false},
    });
    if (!result) {
        co_return make_fail(result.error());
    }

    std::vector<std::pair<std::string, std::string>> links;

    if (result->contains("result") && (*result)["result"].contains("value")) {
        auto& value = (*result)["result"]["value"];
        if (value.is_array()) {
            for (const auto& item : value) {
                links.emplace_back(
                    item.value("text", ""),
                    item.value("href", ""));
            }
        }
    }

    co_return links;
}

auto Snapshot::extract_form_fields() -> awaitable<Result<json>> {
    auto js = R"JS(
        (() => {
            const fields = [];
            const inputs = document.querySelectorAll(
                'input, textarea, select, [contenteditable]'
            );
            for (const el of inputs) {
                const rect = el.getBoundingClientRect();
                const field = {
                    tag: el.tagName.toLowerCase(),
                    type: el.type || '',
                    name: el.name || '',
                    id: el.id || '',
                    value: el.value || '',
                    placeholder: el.placeholder || '',
                    label: '',
                    required: el.required || false,
                    disabled: el.disabled || false,
                    bounding_box: {
                        x: rect.x, y: rect.y,
                        width: rect.width, height: rect.height
                    }
                };
                // Try to find associated label
                if (el.id) {
                    const label = document.querySelector(`label[for="${el.id}"]`);
                    if (label) field.label = label.textContent?.trim() || '';
                }
                if (!field.label && el.closest('label')) {
                    field.label = el.closest('label').textContent?.trim() || '';
                }
                // For select elements, get options
                if (el.tagName === 'SELECT') {
                    field.options = Array.from(el.options).map(o => ({
                        value: o.value,
                        text: o.textContent?.trim() || '',
                        selected: o.selected
                    }));
                }
                fields.push(field);
            }
            return fields;
        })()
    )JS";

    auto result = co_await cdp_.send_command("Runtime.evaluate", {
        {"expression", js},
        {"returnByValue", true},
        {"awaitPromise", false},
    });
    if (!result) {
        co_return make_fail(result.error());
    }

    if (result->contains("result") && (*result)["result"].contains("value")) {
        co_return (*result)["result"]["value"];
    }
    co_return json::array();
}

auto Snapshot::capture_full(const SnapshotOptions& options)
    -> awaitable<Result<json>> {
    json snapshot;

    // Capture DOM
    auto dom = co_await capture_dom(options);
    if (dom) {
        snapshot["dom"] = *dom;
    }

    // Capture accessibility tree
    auto a11y = co_await capture_accessibility_tree();
    if (a11y) {
        snapshot["accessibility"] = *a11y;
    }

    // Extract links
    auto links = co_await extract_links();
    if (links) {
        json links_json = json::array();
        for (const auto& [text, href] : *links) {
            links_json.push_back({{"text", text}, {"href", href}});
        }
        snapshot["links"] = links_json;
    }

    // Extract form fields
    auto forms = co_await extract_form_fields();
    if (forms) {
        snapshot["form_fields"] = *forms;
    }

    // URL and title
    auto url_result = co_await cdp_.send_command("Runtime.evaluate", {
        {"expression", "window.location.href"},
        {"returnByValue", true},
    });
    if (url_result && url_result->contains("result") &&
        (*url_result)["result"].contains("value")) {
        snapshot["url"] = (*url_result)["result"]["value"];
    }

    auto title_result = co_await cdp_.send_command("Runtime.evaluate", {
        {"expression", "document.title"},
        {"returnByValue", true},
    });
    if (title_result && title_result->contains("result") &&
        (*title_result)["result"].contains("value")) {
        snapshot["title"] = (*title_result)["result"]["value"];
    }

    co_return snapshot;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

auto Snapshot::build_dom_tree(const json& nodes, const json& strings,
                              int index, int depth,
                              const SnapshotOptions& options)
    -> DomNode {
    DomNode node;

    if (options.max_depth >= 0 && depth > options.max_depth) {
        return node;
    }

    // DOMSnapshot format uses index-based arrays
    if (!nodes.contains("nodeName") ||
        index < 0 || index >= static_cast<int>(nodes["nodeName"].size())) {
        return node;
    }

    // Node name from string table
    int name_idx = nodes["nodeName"][index].get<int>();
    if (name_idx >= 0 && name_idx < static_cast<int>(strings.size())) {
        node.tag_name = strings[name_idx].get<std::string>();
    }

    // Node type
    int node_type = nodes["nodeType"][index].get<int>();
    switch (node_type) {
        case 1: node.node_type = "element"; break;
        case 3: node.node_type = "text"; break;
        case 9: node.node_type = "document"; break;
        case 10: node.node_type = "doctype"; break;
        case 11: node.node_type = "fragment"; break;
        default: node.node_type = "other"; break;
    }

    node.node_id = index;

    // Text value from string table
    if (nodes.contains("nodeValue")) {
        int val_idx = nodes["nodeValue"][index].get<int>();
        if (val_idx >= 0 && val_idx < static_cast<int>(strings.size())) {
            node.text_content = strings[val_idx].get<std::string>();
        }
    }

    // Skip whitespace-only text nodes if configured
    if (!options.include_whitespace && node.node_type == "text") {
        bool all_whitespace = true;
        for (char c : node.text_content) {
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                all_whitespace = false;
                break;
            }
        }
        if (all_whitespace && !node.text_content.empty()) {
            node.text_content.clear();
        }
    }

    // Attributes
    if (nodes.contains("attributes") && index < static_cast<int>(nodes["attributes"].size())) {
        auto& attrs = nodes["attributes"][index];
        if (attrs.is_array()) {
            for (size_t i = 0; i + 1 < attrs.size(); i += 2) {
                int key_idx = attrs[i].get<int>();
                int val_idx = attrs[i + 1].get<int>();
                std::string key, val;
                if (key_idx >= 0 && key_idx < static_cast<int>(strings.size())) {
                    key = strings[key_idx].get<std::string>();
                }
                if (val_idx >= 0 && val_idx < static_cast<int>(strings.size())) {
                    val = strings[val_idx].get<std::string>();
                }
                if (!key.empty()) {
                    node.attributes[key] = val;
                }
            }
        }
    }

    // Layout / bounding box
    if (nodes.contains("layout")) {
        auto& layout = nodes["layout"];
        if (layout.contains("bounds") && index < static_cast<int>(layout["bounds"].size())) {
            auto& bounds = layout["bounds"][index];
            if (bounds.is_array() && bounds.size() >= 4) {
                node.bounding_box = {
                    {"x", bounds[0]},
                    {"y", bounds[1]},
                    {"width", bounds[2]},
                    {"height", bounds[3]},
                };
            }
        }
    }

    // Children
    if (nodes.contains("parentIndex")) {
        auto& parent_indices = nodes["parentIndex"];
        for (int i = 0; i < static_cast<int>(parent_indices.size()); ++i) {
            if (parent_indices[i].get<int>() == index && i != index) {
                auto child = build_dom_tree(nodes, strings, i, depth + 1, options);
                if (!child.tag_name.empty() || !child.text_content.empty() ||
                    !child.children.empty()) {
                    node.children.push_back(std::move(child));
                }
            }
        }
    }

    return node;
}

auto Snapshot::build_accessibility_tree(const json& node) -> AccessibilityNode {
    AccessibilityNode a11y;

    if (node.contains("role") && node["role"].contains("value")) {
        a11y.role = node["role"]["value"].get<std::string>();
    }
    if (node.contains("name") && node["name"].contains("value")) {
        a11y.name = node["name"]["value"].get<std::string>();
    }
    if (node.contains("value") && node["value"].contains("value")) {
        a11y.value = node["value"]["value"].get<std::string>();
    }
    a11y.ignored = node.value("ignored", false);

    // Collect properties
    if (node.contains("properties")) {
        for (const auto& prop : node["properties"]) {
            auto name = prop.value("name", "");
            if (prop.contains("value") && prop["value"].contains("value")) {
                a11y.properties[name] = prop["value"]["value"];
            }
        }
    }

    return a11y;
}

auto Snapshot::flatten_text(const DomNode& node) -> std::string {
    std::string result;

    if (!node.text_content.empty()) {
        result += node.text_content;
    }

    for (const auto& child : node.children) {
        auto child_text = flatten_text(child);
        if (!child_text.empty()) {
            if (!result.empty() && result.back() != ' ' && result.back() != '\n') {
                result += ' ';
            }
            result += child_text;
        }
    }

    return result;
}

auto Snapshot::render_accessibility_node(const AccessibilityNode& node,
                                         int indent) -> std::string {
    std::ostringstream oss;

    if (node.ignored && node.children.empty()) {
        return {};
    }

    std::string prefix(indent * 2, ' ');

    // Skip generic/static roles that don't add information
    bool is_meaningful = !node.role.empty() &&
                         node.role != "none" &&
                         node.role != "generic" &&
                         node.role != "GenericContainer";

    if (is_meaningful) {
        oss << prefix << "[" << node.role << "]";
        if (!node.name.empty()) {
            oss << " \"" << node.name << "\"";
        }
        if (!node.value.empty()) {
            oss << " = \"" << node.value << "\"";
        }

        // Add relevant ARIA properties
        for (auto& [key, val] : node.properties.items()) {
            if (key == "focusable" || key == "required" || key == "disabled" ||
                key == "checked" || key == "expanded" || key == "selected") {
                if (val.is_boolean() && val.get<bool>()) {
                    oss << " (" << key << ")";
                } else if (val.is_string() && val.get<std::string>() == "true") {
                    oss << " (" << key << ")";
                }
            }
        }

        oss << "\n";
    }

    for (const auto& child : node.children) {
        auto child_text = render_accessibility_node(
            child, is_meaningful ? indent + 1 : indent);
        if (!child_text.empty()) {
            oss << child_text;
        }
    }

    return oss.str();
}

} // namespace openclaw::browser
