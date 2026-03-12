#include <catch2/catch_test_macros.hpp>
#include "openclaw/gateway/protocol.hpp"

using namespace openclaw::gateway;

TEST_CASE("Protocol registers v2026.3.11 methods", "[gateway]") {
    Protocol protocol;
    protocol.register_builtins();

    SECTION("sessions.get is registered") {
        CHECK(protocol.has_method("sessions.get"));
    }

    SECTION("config.schema.lookup is registered") {
        CHECK(protocol.has_method("config.schema.lookup"));
    }

    SECTION("node.pending.enqueue is registered") {
        CHECK(protocol.has_method("node.pending.enqueue"));
    }

    SECTION("node.pending.drain is registered") {
        CHECK(protocol.has_method("node.pending.drain"));
    }

    SECTION("all new methods appear in methods list") {
        auto all_methods = protocol.methods();
        std::vector<std::string> names;
        for (const auto& m : all_methods) names.push_back(m.name);

        CHECK(std::find(names.begin(), names.end(), "sessions.get") != names.end());
        CHECK(std::find(names.begin(), names.end(), "node.pending.enqueue") != names.end());
        CHECK(std::find(names.begin(), names.end(), "node.pending.drain") != names.end());
    }
}
