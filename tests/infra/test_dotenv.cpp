#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "openclaw/infra/dotenv.hpp"

namespace fs = std::filesystem;

static auto write_env_file(const std::string& content) -> fs::path {
    auto tmp = fs::temp_directory_path() / "test_dotenv_file.env";
    std::ofstream out(tmp);
    out << content;
    out.close();
    return tmp;
}

TEST_CASE("dotenv parse basic KEY=VALUE", "[infra][dotenv]") {
    auto path = write_env_file("FOO=bar\nBAZ=qux\n");
    auto env = openclaw::infra::parse(path);

    REQUIRE(env.size() == 2);
    CHECK(env["FOO"] == "bar");
    CHECK(env["BAZ"] == "qux");

    fs::remove(path);
}

TEST_CASE("dotenv parse double-quoted values", "[infra][dotenv]") {
    auto path = write_env_file(R"(MSG="hello world")" "\n"
                               R"(ESCAPED="line1\nline2")" "\n");
    auto env = openclaw::infra::parse(path);

    CHECK(env["MSG"] == "hello world");
    CHECK(env["ESCAPED"] == "line1\nline2");

    fs::remove(path);
}

TEST_CASE("dotenv parse single-quoted values (literal)", "[infra][dotenv]") {
    auto path = write_env_file("LITERAL='hello\\nworld'\n");
    auto env = openclaw::infra::parse(path);

    // Single-quoted values should be literal (no escape processing)
    CHECK(env["LITERAL"] == "hello\\nworld");

    fs::remove(path);
}

TEST_CASE("dotenv parse skips comments and empty lines", "[infra][dotenv]") {
    auto path = write_env_file(
        "# This is a comment\n"
        "\n"
        "KEY1=value1\n"
        "  \n"
        "# Another comment\n"
        "KEY2=value2\n"
    );
    auto env = openclaw::infra::parse(path);

    REQUIRE(env.size() == 2);
    CHECK(env["KEY1"] == "value1");
    CHECK(env["KEY2"] == "value2");

    fs::remove(path);
}

TEST_CASE("dotenv parse inline comments on unquoted values", "[infra][dotenv]") {
    auto path = write_env_file("PORT=8080 # server port\nHOST=localhost\n");
    auto env = openclaw::infra::parse(path);

    CHECK(env["PORT"] == "8080");
    CHECK(env["HOST"] == "localhost");

    fs::remove(path);
}

TEST_CASE("dotenv parse export prefix", "[infra][dotenv]") {
    auto path = write_env_file("export API_KEY=secret123\nexport DEBUG=true\n");
    auto env = openclaw::infra::parse(path);

    REQUIRE(env.size() == 2);
    CHECK(env["API_KEY"] == "secret123");
    CHECK(env["DEBUG"] == "true");

    fs::remove(path);
}

TEST_CASE("dotenv parse skips malformed lines", "[infra][dotenv]") {
    auto path = write_env_file(
        "GOOD=value\n"
        "no_equals_sign\n"
        "ALSO_GOOD=another\n"
    );
    auto env = openclaw::infra::parse(path);

    CHECK(env.size() == 2);
    CHECK(env.count("GOOD") == 1);
    CHECK(env.count("ALSO_GOOD") == 1);

    fs::remove(path);
}

TEST_CASE("dotenv parse returns empty for missing file", "[infra][dotenv]") {
    auto env = openclaw::infra::parse("/nonexistent/path/.env");
    CHECK(env.empty());
}

TEST_CASE("dotenv parse handles empty values", "[infra][dotenv]") {
    auto path = write_env_file("EMPTY=\nNOTEMPTY=something\n");
    auto env = openclaw::infra::parse(path);

    REQUIRE(env.size() == 2);
    CHECK(env["EMPTY"] == "");
    CHECK(env["NOTEMPTY"] == "something");

    fs::remove(path);
}

TEST_CASE("dotenv parse double-quoted escape sequences", "[infra][dotenv]") {
    auto path = write_env_file(
        R"(TAB="hello\tworld")" "\n"
        R"(QUOTE="say \"hi\"")" "\n"
        R"(BACKSLASH="path\\to\\file")" "\n"
    );
    auto env = openclaw::infra::parse(path);

    CHECK(env["TAB"] == "hello\tworld");
    CHECK(env["QUOTE"] == "say \"hi\"");
    CHECK(env["BACKSLASH"] == "path\\to\\file");

    fs::remove(path);
}
