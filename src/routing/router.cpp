#include "openclaw/routing/router.hpp"

#include <algorithm>

#include "openclaw/core/logger.hpp"

namespace openclaw::routing {

void Router::add_route(std::unique_ptr<RoutingRule> rule, Handler handler) {
    LOG_INFO("Adding route: {} (priority={})", rule->name(), rule->priority());
    routes_.push_back(Route{
        .rule = std::move(rule),
        .handler = std::move(handler),
    });
    sort_routes();
}

auto Router::route(const IncomingMessage& msg) -> awaitable<Result<void>> {
    LOG_DEBUG("Routing message from {} on channel '{}': '{}'",
              msg.sender_id, msg.channel,
              msg.text.substr(0, 80));

    for (auto& [rule, handler] : routes_) {
        if (rule->matches(msg)) {
            LOG_DEBUG("Message matched rule '{}'", rule->name());
            try {
                co_await handler(msg);
                co_return ok_result();
            } catch (const std::exception& e) {
                LOG_ERROR("Handler for rule '{}' threw: {}", rule->name(), e.what());
                co_return make_fail(
                    make_error(ErrorCode::InternalError,
                               "Route handler failed", e.what()));
            }
        }
    }

    LOG_WARN("No matching route for message from {} on channel '{}'",
             msg.sender_id, msg.channel);
    co_return make_fail(
        make_error(ErrorCode::NotFound, "No matching route found"));
}

auto Router::route_count() const -> size_t {
    return routes_.size();
}

void Router::clear() {
    routes_.clear();
    LOG_INFO("All routes cleared");
}

void Router::sort_routes() {
    std::ranges::sort(routes_, [](const Route& a, const Route& b) {
        return a.rule->priority() > b.rule->priority();
    });
}

} // namespace openclaw::routing
