#include "openclaw/gateway/skills_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_skills_handlers(Protocol& protocol) {
    // skills.status — list installed skills and their status.
    protocol.register_method("skills.status",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"skills", json::array()},
                {"count", 0},
            };
        },
        "Get status of installed skills", "skills");

    // skills.bins — list required binary dependencies.
    protocol.register_method("skills.bins",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"bins", json::array()},
            };
        },
        "List required binary dependencies for skills", "skills");

    // skills.install — install a skill from a source.
    protocol.register_method("skills.install",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto source = params.value("source", "");
            auto name = params.value("name", "");
            if (source.empty()) {
                co_return json{{"ok", false}, {"error", "source is required"}};
            }
            LOG_INFO("Installing skill: {} from {}", name, source);
            co_return json{
                {"ok", true},
                {"name", name},
                {"source", source},
                {"status", "installed"},
            };
        },
        "Install a skill", "skills");

    // skills.update — update skill configuration.
    protocol.register_method("skills.update",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "name is required"}};
            }
            co_return json{{"ok", true}, {"name", name}};
        },
        "Update skill configuration", "skills");

    LOG_INFO("Registered skills handlers");
}

} // namespace openclaw::gateway
