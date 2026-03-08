#include "openclaw/gateway/wizard_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_wizard_handlers(Protocol& protocol) {
    // wizard.start — begin the setup wizard flow.
    protocol.register_method("wizard.start",
        []([[maybe_unused]] json params) -> awaitable<json> {
            LOG_INFO("Setup wizard started");
            co_return json{
                {"ok", true},
                {"step", 1},
                {"totalSteps", 4},
                {"title", "Welcome"},
                {"description", "Let's set up your AI assistant."},
            };
        },
        "Start the setup wizard", "wizard");

    // wizard.next — advance to the next wizard step.
    protocol.register_method("wizard.next",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto current_step = params.value("step", 1);
            auto answers = params.value("answers", json::object());
            int next_step = current_step + 1;
            bool complete = next_step > 4;

            if (complete) {
                LOG_INFO("Setup wizard completed");
                co_return json{
                    {"ok", true},
                    {"complete", true},
                    {"step", current_step},
                };
            }

            co_return json{
                {"ok", true},
                {"step", next_step},
                {"totalSteps", 4},
                {"complete", false},
            };
        },
        "Advance to next wizard step", "wizard");

    // wizard.cancel — cancel the setup wizard.
    protocol.register_method("wizard.cancel",
        []([[maybe_unused]] json params) -> awaitable<json> {
            LOG_INFO("Setup wizard cancelled");
            co_return json{{"ok", true}};
        },
        "Cancel the setup wizard", "wizard");

    // wizard.status — get current wizard state.
    protocol.register_method("wizard.status",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"active", false},
                {"step", 0},
                {"complete", true},
            };
        },
        "Get wizard status", "wizard");

    LOG_INFO("Registered wizard handlers");
}

} // namespace openclaw::gateway
