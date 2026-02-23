#include <catch2/catch_test_macros.hpp>
#include "openclaw/infra/fetch_guard.hpp"
using namespace openclaw::infra;

TEST_CASE("HTML content sanitization", "[infra][sanitizer]") {
    SECTION("Strips display:none elements") {
        auto result = FetchGuard::sanitize_html_content(
            R"(<p>Visible</p><div style="display:none">Hidden injection</div><p>Also visible</p>)");
        CHECK(result.find("Hidden injection") == std::string::npos);
        CHECK(result.find("Visible") != std::string::npos);
        CHECK(result.find("Also visible") != std::string::npos);
    }
    SECTION("Strips visibility:hidden elements") {
        auto result = FetchGuard::sanitize_html_content(
            R"(<span style="visibility:hidden">Secret</span><span>Public</span>)");
        CHECK(result.find("Secret") == std::string::npos);
        CHECK(result.find("Public") != std::string::npos);
    }
    SECTION("Strips sr-only class elements") {
        auto result = FetchGuard::sanitize_html_content(
            R"(<span class="sr-only">Screen reader only</span><span>Normal</span>)");
        CHECK(result.find("Screen reader only") == std::string::npos);
        CHECK(result.find("Normal") != std::string::npos);
    }
    SECTION("Strips aria-hidden elements") {
        auto result = FetchGuard::sanitize_html_content(
            R"(<div aria-hidden="true">Hidden from AT</div><div>Visible</div>)");
        CHECK(result.find("Hidden from AT") == std::string::npos);
        CHECK(result.find("Visible") != std::string::npos);
    }
    SECTION("Preserves normal content") {
        std::string normal = "<p>Hello world</p><div class='content'>Normal text</div>";
        CHECK(FetchGuard::sanitize_html_content(normal) == normal);
    }
}
