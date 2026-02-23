#include <catch2/catch_test_macros.hpp>
#include "openclaw/providers/volcengine.hpp"

using namespace openclaw::providers;

TEST_CASE("VolcEngine provider basics", "[providers][volcengine]") {
    boost::asio::io_context ioc;
    openclaw::ProviderConfig config;
    config.name = "volcengine";
    config.api_key = "test-key";

    VolcEngineProvider provider(ioc, config);

    SECTION("Name is volcengine") {
        CHECK(provider.name() == "volcengine");
    }

    SECTION("Has model catalog") {
        auto models = provider.models();
        CHECK(!models.empty());
    }
}
