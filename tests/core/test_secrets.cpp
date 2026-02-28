#include <catch2/catch_test_macros.hpp>

#include "openclaw/core/secrets.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace openclaw;

TEST_CASE("SecretResolver: env resolution", "[core][secrets]") {
    SecretsConfig config;
    SecretResolver resolver(config);

    SECTION("reads existing env var") {
        // PATH should always exist
        auto result = resolver.resolve_env("PATH");
        REQUIRE(result.has_value());
        CHECK_FALSE(result->empty());
    }

    SECTION("missing env var returns error") {
        auto result = resolver.resolve_env("DEFINITELY_NOT_SET_12345");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::NotFound);
    }

    SECTION("empty key rejected") {
        auto result = resolver.resolve_env("");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("SecretResolver: env allowlist", "[core][secrets]") {
    SecretsConfig config;
    config.env = SecretsConfig::EnvProvider{};
    config.env->allowlist = {"ALLOWED_KEY"};
    SecretResolver resolver(config);

    SECTION("non-allowlisted key blocked") {
        auto result = resolver.resolve_env("PATH");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::Forbidden);
    }
}

TEST_CASE("SecretResolver: file resolution", "[core][secrets]") {
    namespace fs = std::filesystem;

    auto tmp_dir = fs::temp_directory_path() / ("test_secrets_" + std::to_string(::getpid()));
    fs::create_directories(tmp_dir);

    auto secret_file = tmp_dir / "api_key.txt";
    std::ofstream(secret_file) << "sk-test-12345\n";

    // Set permissions to 0600
#ifndef _WIN32
    ::chmod(secret_file.c_str(), 0600);
#endif

    SecretsConfig config;
    SecretResolver resolver(config);

    SECTION("reads file and trims trailing newline") {
        auto result = resolver.resolve_file(secret_file.string());
        REQUIRE(result.has_value());
        CHECK(*result == "sk-test-12345");
    }

    SECTION("missing file returns error") {
        auto result = resolver.resolve_file("/nonexistent/file.txt");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("empty path rejected") {
        auto result = resolver.resolve_file("");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidArgument);
    }

    // Cleanup
    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
}

TEST_CASE("SecretResolver: exec resolution", "[core][secrets]") {
    SecretsConfig config;
    SecretResolver resolver(config);

    SECTION("echo returns value") {
        auto result = resolver.resolve_exec("echo", {"hello"});
        REQUIRE(result.has_value());
        CHECK(*result == "hello");
    }

    SECTION("empty command rejected") {
        auto result = resolver.resolve_exec("", {});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("SecretResolver: resolve dispatches by source", "[core][secrets]") {
    SecretsConfig config;
    SecretResolver resolver(config);

    SECTION("env source") {
        SecretRef ref{"env", "", "PATH"};
        auto result = resolver.resolve(ref);
        REQUIRE(result.has_value());
    }

    SECTION("unknown source rejected") {
        SecretRef ref{"vault", "hashicorp", "secret/data/key"};
        auto result = resolver.resolve(ref);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidArgument);
    }
}
