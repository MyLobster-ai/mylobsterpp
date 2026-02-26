#include "openclaw/gateway/protocol.hpp"

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <boost/asio/use_awaitable.hpp>

namespace openclaw::gateway {

Protocol::Protocol() = default;

void Protocol::register_method(std::string name, MethodHandler handler,
                               std::string description, std::string group) {
    LOG_DEBUG("Registering method: {}", name);
    methods_[name] = Entry{
        .handler = std::move(handler),
        .info = MethodInfo{
            .name = name,
            .description = std::move(description),
            .group = std::move(group),
        },
    };
}

auto Protocol::has_method(std::string_view name) const -> bool {
    return methods_.contains(std::string(name));
}

auto Protocol::methods() const -> std::vector<MethodInfo> {
    std::vector<MethodInfo> result;
    result.reserve(methods_.size());
    for (const auto& [_, entry] : methods_) {
        result.push_back(entry.info);
    }
    return result;
}

auto Protocol::methods_in_group(std::string_view group) const
    -> std::vector<MethodInfo> {
    std::vector<MethodInfo> result;
    for (const auto& [_, entry] : methods_) {
        if (entry.info.group == group) {
            result.push_back(entry.info);
        }
    }
    return result;
}

auto Protocol::dispatch(const RequestFrame& request) -> awaitable<Result<json>> {
    auto it = methods_.find(request.method);
    if (it == methods_.end()) {
        co_return make_fail(
            make_error(ErrorCode::NotFound,
                       "Method not found: " + request.method));
    }

    try {
        auto result = co_await it->second.handler(request.params);
        co_return result;
    } catch (const std::exception& e) {
        LOG_ERROR("Method {} threw exception: {}", request.method, e.what());
        co_return make_fail(
            make_error(ErrorCode::InternalError,
                       "Method execution failed", e.what()));
    }
}

// ---------------------------------------------------------------------------
// Built-in method stubs
// ---------------------------------------------------------------------------

namespace {

/// Helper to create a stub handler that returns a "not implemented" response
/// with the method name embedded.
auto make_stub(std::string_view method_name) -> MethodHandler {
    auto name = std::string(method_name);
    return [name]([[maybe_unused]] json params) -> awaitable<json> {
        co_return json{
            {"status", "not_implemented"},
            {"method", name},
            {"message", "This method is registered but not yet connected to a subsystem."},
        };
    };
}

} // anonymous namespace

void Protocol::register_builtins() {
    register_gateway_methods();
    register_session_methods();
    register_channel_methods();
    register_tool_methods();
    register_memory_methods();
    register_browser_methods();
    register_provider_methods();
    register_plugin_methods();
    register_agent_methods();
    register_cron_methods();
    register_config_methods();

    LOG_INFO("Registered {} built-in method stubs", methods_.size());
}

void Protocol::register_gateway_methods() {
    constexpr std::string_view g = "gateway";

    register_method("gateway.info", make_stub("gateway.info"),
        "Return gateway version and capabilities", std::string(g));
    register_method("gateway.ping", make_stub("gateway.ping"),
        "Health check ping", std::string(g));
    register_method("gateway.status", make_stub("gateway.status"),
        "Return gateway runtime status (uptime, connections, load)", std::string(g));
    register_method("gateway.methods", make_stub("gateway.methods"),
        "List all registered RPC methods", std::string(g));
    register_method("gateway.subscribe", make_stub("gateway.subscribe"),
        "Subscribe to server-sent events by topic", std::string(g));
    register_method("gateway.unsubscribe", make_stub("gateway.unsubscribe"),
        "Unsubscribe from server-sent events", std::string(g));
    register_method("gateway.shutdown", make_stub("gateway.shutdown"),
        "Initiate graceful server shutdown", std::string(g));
    register_method("gateway.reload", make_stub("gateway.reload"),
        "Reload gateway configuration", std::string(g));
    register_method("gateway.metrics", make_stub("gateway.metrics"),
        "Return gateway metrics (requests, latencies, errors)", std::string(g));
    register_method("gateway.logs", make_stub("gateway.logs"),
        "Stream or query recent gateway logs", std::string(g));
}

void Protocol::register_session_methods() {
    constexpr std::string_view g = "session";

    register_method("session.create", make_stub("session.create"),
        "Create a new user session", std::string(g));
    register_method("session.get", make_stub("session.get"),
        "Get session details by id", std::string(g));
    register_method("session.list", make_stub("session.list"),
        "List active sessions", std::string(g));
    register_method("session.destroy", make_stub("session.destroy"),
        "Destroy / end a session", std::string(g));
    register_method("session.heartbeat", make_stub("session.heartbeat"),
        "Keep a session alive", std::string(g));
    register_method("session.update", make_stub("session.update"),
        "Update session metadata", std::string(g));
    register_method("session.context.set", make_stub("session.context.set"),
        "Set session context variables", std::string(g));
    register_method("session.context.get", make_stub("session.context.get"),
        "Get session context variables", std::string(g));
    register_method("session.context.clear", make_stub("session.context.clear"),
        "Clear session context", std::string(g));
    register_method("session.history", make_stub("session.history"),
        "Get session message history", std::string(g));
}

void Protocol::register_channel_methods() {
    constexpr std::string_view g = "channel";

    register_method("channel.list", make_stub("channel.list"),
        "List available communication channels", std::string(g));
    register_method("channel.connect", make_stub("channel.connect"),
        "Connect / enable a channel", std::string(g));
    register_method("channel.disconnect", make_stub("channel.disconnect"),
        "Disconnect / disable a channel", std::string(g));
    register_method("channel.status", make_stub("channel.status"),
        "Get channel connection status", std::string(g));
    register_method("channel.send", make_stub("channel.send"),
        "Send a message through a channel", std::string(g));
    register_method("channel.receive", make_stub("channel.receive"),
        "Poll for messages from a channel", std::string(g));
    register_method("channel.configure", make_stub("channel.configure"),
        "Update channel configuration", std::string(g));
    register_method("channel.telegram.webhook", make_stub("channel.telegram.webhook"),
        "Register Telegram webhook", std::string(g));
    register_method("channel.discord.setup", make_stub("channel.discord.setup"),
        "Set up Discord bot connection", std::string(g));
    register_method("channel.slack.setup", make_stub("channel.slack.setup"),
        "Set up Slack bot connection", std::string(g));
    register_method("channel.whatsapp.setup", make_stub("channel.whatsapp.setup"),
        "Set up WhatsApp Business API connection", std::string(g));
    register_method("channel.sms.send", make_stub("channel.sms.send"),
        "Send an SMS via Twilio", std::string(g));
}

void Protocol::register_tool_methods() {
    constexpr std::string_view g = "tool";

    register_method("tool.list", make_stub("tool.list"),
        "List all registered tools", std::string(g));
    register_method("tool.execute", make_stub("tool.execute"),
        "Execute a tool by name with params", std::string(g));
    register_method("tool.register", make_stub("tool.register"),
        "Register a new dynamic tool", std::string(g));
    register_method("tool.unregister", make_stub("tool.unregister"),
        "Unregister a dynamic tool", std::string(g));
    register_method("tool.describe", make_stub("tool.describe"),
        "Get tool schema and description", std::string(g));
    register_method("tool.enable", make_stub("tool.enable"),
        "Enable a disabled tool", std::string(g));
    register_method("tool.disable", make_stub("tool.disable"),
        "Disable a tool without unregistering", std::string(g));
    register_method("tool.shell.exec", make_stub("tool.shell.exec"),
        "Execute a shell command", std::string(g));
    register_method("tool.file.read", make_stub("tool.file.read"),
        "Read file contents", std::string(g));
    register_method("tool.file.write", make_stub("tool.file.write"),
        "Write file contents", std::string(g));
    register_method("tool.file.list", make_stub("tool.file.list"),
        "List directory contents", std::string(g));
    register_method("tool.file.search", make_stub("tool.file.search"),
        "Search files by pattern", std::string(g));
    register_method("tool.http.request", make_stub("tool.http.request"),
        "Make an HTTP request", std::string(g));
    register_method("tool.code.run", make_stub("tool.code.run"),
        "Execute code in a sandboxed runtime", std::string(g));
    register_method("tool.code.analyze", make_stub("tool.code.analyze"),
        "Analyze code for issues", std::string(g));
}

void Protocol::register_memory_methods() {
    constexpr std::string_view g = "memory";

    register_method("memory.store", make_stub("memory.store"),
        "Store a memory/fact", std::string(g));
    register_method("memory.recall", make_stub("memory.recall"),
        "Recall memories by semantic query", std::string(g));
    register_method("memory.search", make_stub("memory.search"),
        "Search memories with filters", std::string(g));
    register_method("memory.delete", make_stub("memory.delete"),
        "Delete a specific memory", std::string(g));
    register_method("memory.list", make_stub("memory.list"),
        "List stored memories", std::string(g));
    register_method("memory.clear", make_stub("memory.clear"),
        "Clear all memories for a scope", std::string(g));
    register_method("memory.stats", make_stub("memory.stats"),
        "Return memory store statistics", std::string(g));
    register_method("memory.embed", make_stub("memory.embed"),
        "Generate embedding for text", std::string(g));
    register_method("memory.index.rebuild", make_stub("memory.index.rebuild"),
        "Rebuild the vector index", std::string(g));
    register_method("memory.rag.query", make_stub("memory.rag.query"),
        "RAG query: retrieve context and generate response", std::string(g));
}

void Protocol::register_browser_methods() {
    constexpr std::string_view g = "browser";

    register_method("browser.open", make_stub("browser.open"),
        "Open a URL in headless browser", std::string(g));
    register_method("browser.close", make_stub("browser.close"),
        "Close a browser page", std::string(g));
    register_method("browser.navigate", make_stub("browser.navigate"),
        "Navigate to a URL", std::string(g));
    register_method("browser.screenshot", make_stub("browser.screenshot"),
        "Take a screenshot", std::string(g));
    register_method("browser.content", make_stub("browser.content"),
        "Get page content as text/html", std::string(g));
    register_method("browser.click", make_stub("browser.click"),
        "Click an element on the page", std::string(g));
    register_method("browser.type", make_stub("browser.type"),
        "Type text into an input field", std::string(g));
    register_method("browser.evaluate", make_stub("browser.evaluate"),
        "Evaluate JavaScript on the page", std::string(g));
    register_method("browser.wait", make_stub("browser.wait"),
        "Wait for a selector or condition", std::string(g));
    register_method("browser.scroll", make_stub("browser.scroll"),
        "Scroll the page", std::string(g));
    register_method("browser.pdf", make_stub("browser.pdf"),
        "Export page as PDF", std::string(g));
    register_method("browser.cookies.get", make_stub("browser.cookies.get"),
        "Get browser cookies", std::string(g));
    register_method("browser.cookies.set", make_stub("browser.cookies.set"),
        "Set browser cookies", std::string(g));
}

void Protocol::register_provider_methods() {
    constexpr std::string_view g = "provider";

    register_method("provider.list", make_stub("provider.list"),
        "List configured AI providers", std::string(g));
    register_method("provider.chat", make_stub("provider.chat"),
        "Send a chat completion request", std::string(g));
    register_method("provider.chat.stream", make_stub("provider.chat.stream"),
        "Stream a chat completion", std::string(g));
    register_method("provider.models", make_stub("provider.models"),
        "List available models for a provider", std::string(g));
    register_method("provider.embed", make_stub("provider.embed"),
        "Generate embeddings via a provider", std::string(g));
    register_method("provider.status", make_stub("provider.status"),
        "Check provider availability", std::string(g));
    register_method("provider.configure", make_stub("provider.configure"),
        "Update provider configuration at runtime", std::string(g));
    register_method("provider.usage", make_stub("provider.usage"),
        "Get token/cost usage statistics", std::string(g));
}

void Protocol::register_plugin_methods() {
    constexpr std::string_view g = "plugin";

    register_method("plugin.list", make_stub("plugin.list"),
        "List installed plugins", std::string(g));
    register_method("plugin.install", make_stub("plugin.install"),
        "Install a plugin from path or URL", std::string(g));
    register_method("plugin.uninstall", make_stub("plugin.uninstall"),
        "Uninstall a plugin", std::string(g));
    register_method("plugin.enable", make_stub("plugin.enable"),
        "Enable an installed plugin", std::string(g));
    register_method("plugin.disable", make_stub("plugin.disable"),
        "Disable a plugin", std::string(g));
    register_method("plugin.configure", make_stub("plugin.configure"),
        "Update plugin settings", std::string(g));
    register_method("plugin.call", make_stub("plugin.call"),
        "Call an exported plugin function", std::string(g));
    register_method("plugin.status", make_stub("plugin.status"),
        "Get plugin runtime status", std::string(g));
}

void Protocol::register_agent_methods() {
    constexpr std::string_view g = "agent";

    register_method("agent.chat", make_stub("agent.chat"),
        "Send a message to the agent and get a response", std::string(g));
    register_method("agent.chat.stream", make_stub("agent.chat.stream"),
        "Stream agent chat response", std::string(g));
    register_method("agent.chat.cancel", make_stub("agent.chat.cancel"),
        "Cancel an in-progress agent response", std::string(g));
    register_method("agent.system_prompt.get", make_stub("agent.system_prompt.get"),
        "Get the current system prompt", std::string(g));
    register_method("agent.system_prompt.set", make_stub("agent.system_prompt.set"),
        "Set the system prompt", std::string(g));
    register_method("agent.thinking.set", make_stub("agent.thinking.set"),
        "Set thinking mode (none, basic, extended)", std::string(g));
    register_method("agent.thinking.get", make_stub("agent.thinking.get"),
        "Get current thinking mode", std::string(g));
    register_method("agent.model.set", make_stub("agent.model.set"),
        "Set the active model", std::string(g));
    register_method("agent.model.get", make_stub("agent.model.get"),
        "Get the active model", std::string(g));
    register_method("agent.conversation.create", make_stub("agent.conversation.create"),
        "Create a new conversation", std::string(g));
    register_method("agent.conversation.list", make_stub("agent.conversation.list"),
        "List conversations", std::string(g));
    register_method("agent.conversation.get", make_stub("agent.conversation.get"),
        "Get conversation details and messages", std::string(g));
    register_method("agent.conversation.delete", make_stub("agent.conversation.delete"),
        "Delete a conversation", std::string(g));
    register_method("agent.conversation.rename", make_stub("agent.conversation.rename"),
        "Rename a conversation", std::string(g));
}

void Protocol::register_cron_methods() {
    constexpr std::string_view g = "cron";

    register_method("cron.list", make_stub("cron.list"),
        "List scheduled tasks", std::string(g));
    register_method("cron.create", make_stub("cron.create"),
        "Create a scheduled task", std::string(g));
    register_method("cron.delete", make_stub("cron.delete"),
        "Delete a scheduled task", std::string(g));
    register_method("cron.enable", make_stub("cron.enable"),
        "Enable a scheduled task", std::string(g));
    register_method("cron.disable", make_stub("cron.disable"),
        "Disable a scheduled task", std::string(g));
    register_method("cron.trigger", make_stub("cron.trigger"),
        "Manually trigger a scheduled task", std::string(g));
    register_method("cron.status", make_stub("cron.status"),
        "Get cron scheduler status", std::string(g));
}

void Protocol::register_config_methods() {
    constexpr std::string_view g = "config";

    register_method("config.get", make_stub("config.get"),
        "Get configuration value by key", std::string(g));
    register_method("config.set", make_stub("config.set"),
        "Set configuration value", std::string(g));
    register_method("config.list", make_stub("config.list"),
        "List all configuration keys", std::string(g));
    register_method("config.reset", make_stub("config.reset"),
        "Reset configuration to defaults", std::string(g));
    register_method("config.export", make_stub("config.export"),
        "Export full configuration as JSON", std::string(g));
    register_method("config.import", make_stub("config.import"),
        "Import configuration from JSON", std::string(g));
}

} // namespace openclaw::gateway
