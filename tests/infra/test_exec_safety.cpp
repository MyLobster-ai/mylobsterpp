#include <catch2/catch_test_macros.hpp>

#include "openclaw/infra/exec_safety.hpp"

using namespace openclaw::infra;

// ---------------------------------------------------------------------------
// is_shell_wrapper
// ---------------------------------------------------------------------------

TEST_CASE("Known shell wrappers detected", "[infra][exec_safety]") {
    CHECK(is_shell_wrapper("sh"));
    CHECK(is_shell_wrapper("bash"));
    CHECK(is_shell_wrapper("env"));
    CHECK(is_shell_wrapper("sudo"));
    CHECK(is_shell_wrapper("timeout"));
}

TEST_CASE("Regular binaries are not wrappers", "[infra][exec_safety]") {
    CHECK_FALSE(is_shell_wrapper("python"));
    CHECK_FALSE(is_shell_wrapper("node"));
    CHECK_FALSE(is_shell_wrapper("cat"));
}

// ---------------------------------------------------------------------------
// unwrap_shell_wrapper_argv
// ---------------------------------------------------------------------------

TEST_CASE("Direct command returns index 0", "[infra][exec_safety]") {
    std::vector<std::string> argv = {"python", "script.py"};
    auto idx = unwrap_shell_wrapper_argv(argv);
    REQUIRE(idx.has_value());
    CHECK(*idx == 0);
}

TEST_CASE("Single wrapper unwraps to index 1", "[infra][exec_safety]") {
    std::vector<std::string> argv = {"env", "python", "script.py"};
    auto idx = unwrap_shell_wrapper_argv(argv);
    REQUIRE(idx.has_value());
    CHECK(*idx == 1);
}

TEST_CASE("Nested wrappers unwrap correctly", "[infra][exec_safety]") {
    std::vector<std::string> argv = {"sudo", "env", "python", "script.py"};
    auto idx = unwrap_shell_wrapper_argv(argv);
    REQUIRE(idx.has_value());
    CHECK(*idx == 2);
}

TEST_CASE("sh -c returns inline command index", "[infra][exec_safety]") {
    std::vector<std::string> argv = {"sh", "-c", "echo hello"};
    auto idx = unwrap_shell_wrapper_argv(argv);
    REQUIRE(idx.has_value());
    CHECK(*idx == 2);
}

TEST_CASE("Depth cap exceeded returns nullopt", "[infra][exec_safety]") {
    // Create deeply nested wrappers exceeding the cap
    std::vector<std::string> argv;
    for (int i = 0; i < kMaxUnwrapDepth + 1; ++i) {
        argv.push_back("env");
    }
    argv.push_back("python");

    auto idx = unwrap_shell_wrapper_argv(argv);
    CHECK_FALSE(idx.has_value());
}

// ---------------------------------------------------------------------------
// resolve_inline_command_token_index
// ---------------------------------------------------------------------------

TEST_CASE("Finds -c flag in argv", "[infra][exec_safety]") {
    std::vector<std::string> argv = {"bash", "-c", "echo test"};
    auto idx = resolve_inline_command_token_index(argv);
    REQUIRE(idx.has_value());
    CHECK(*idx == 2);
}

TEST_CASE("Returns nullopt when no -c flag", "[infra][exec_safety]") {
    std::vector<std::string> argv = {"python", "script.py"};
    auto idx = resolve_inline_command_token_index(argv);
    CHECK_FALSE(idx.has_value());
}

// ---------------------------------------------------------------------------
// has_trailing_positional_argv
// ---------------------------------------------------------------------------

TEST_CASE("Detects trailing positional arguments", "[infra][exec_safety]") {
    std::vector<std::string> argv = {"python", "script.py", "arg1"};
    CHECK(has_trailing_positional_argv(argv, 1));
}

TEST_CASE("No trailing positionals after last arg", "[infra][exec_safety]") {
    std::vector<std::string> argv = {"python", "script.py"};
    CHECK_FALSE(has_trailing_positional_argv(argv, 1));
}

// ---------------------------------------------------------------------------
// validate_system_run_consistency
// ---------------------------------------------------------------------------

TEST_CASE("Consistent argv passes validation", "[infra][exec_safety]") {
    std::vector<std::string> argv = {"python", "script.py"};
    CHECK(validate_system_run_consistency(argv, "python"));
}

TEST_CASE("Wrapped consistent argv passes", "[infra][exec_safety]") {
    std::vector<std::string> argv = {"env", "python", "script.py"};
    CHECK(validate_system_run_consistency(argv, "python"));
}

TEST_CASE("Inconsistent declared command fails", "[infra][exec_safety]") {
    std::vector<std::string> argv = {"python", "script.py"};
    CHECK_FALSE(validate_system_run_consistency(argv, "ruby"));
}

TEST_CASE("Empty argv fails validation", "[infra][exec_safety]") {
    std::vector<std::string> argv;
    CHECK_FALSE(validate_system_run_consistency(argv, "python"));
}
