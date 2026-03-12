#include <catch2/catch_test_macros.hpp>
#include "openclaw/channels/matrix.hpp"

using namespace openclaw::channels;

TEST_CASE("MatrixConfig defaults", "[channels][matrix]") {
    MatrixConfig config;
    config.homeserver_url = "https://matrix.org";
    config.access_token = "test_token";

    CHECK(config.channel_name == "matrix");
    CHECK(config.room_bindings.is_null());
}
