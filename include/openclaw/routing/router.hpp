#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/error.hpp"
#include "openclaw/routing/rules.hpp"

namespace openclaw::routing {

using boost::asio::awaitable;

class Router {
public:
    using Handler = std::function<awaitable<void>(const IncomingMessage&)>;

    Router() = default;

    void add_route(std::unique_ptr<RoutingRule> rule, Handler handler);

    auto route(const IncomingMessage& msg) -> awaitable<Result<void>>;

    [[nodiscard]] auto route_count() const -> size_t;

    void clear();

private:
    struct Route {
        std::unique_ptr<RoutingRule> rule;
        Handler handler;
    };

    std::vector<Route> routes_;

    void sort_routes();
};

} // namespace openclaw::routing
