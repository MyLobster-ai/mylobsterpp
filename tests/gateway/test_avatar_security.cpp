#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include "openclaw/gateway/server.hpp"
using namespace openclaw::gateway;
namespace fs = std::filesystem;

TEST_CASE("Avatar path validation", "[gateway][security]") {
    // Create a temp directory for testing
    auto tmp = fs::temp_directory_path() / "openclaw_avatar_test";
    fs::create_directories(tmp);

    // Create a valid avatar file
    {
        std::ofstream f(tmp / "avatar.png");
        f << "fake png data";
    }

    SECTION("Valid avatar within root succeeds") {
        auto result = GatewayServer::validate_avatar_path(tmp / "avatar.png", tmp);
        CHECK(result.has_value());
    }

    SECTION("Non-existent file fails") {
        auto result = GatewayServer::validate_avatar_path(tmp / "nonexistent.png", tmp);
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == openclaw::ErrorCode::NotFound);
    }

    // Cleanup
    fs::remove_all(tmp);
}
