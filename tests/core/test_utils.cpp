#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

#include "openclaw/core/utils.hpp"

TEST_CASE("generate_id produces expected length", "[utils]") {
    SECTION("default length") {
        auto id = openclaw::utils::generate_id();
        REQUIRE(id.size() == 16);
    }

    SECTION("custom length") {
        auto id = openclaw::utils::generate_id(32);
        REQUIRE(id.size() == 32);
    }

    SECTION("zero length") {
        auto id = openclaw::utils::generate_id(0);
        REQUIRE(id.empty());
    }

    SECTION("contains only lowercase alphanumeric characters") {
        auto id = openclaw::utils::generate_id(100);
        for (char c : id) {
            bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            CHECK(valid);
        }
    }

    SECTION("successive calls produce different IDs") {
        auto a = openclaw::utils::generate_id(16);
        auto b = openclaw::utils::generate_id(16);
        CHECK(a != b);
    }
}

TEST_CASE("generate_uuid produces valid format", "[utils]") {
    auto uuid = openclaw::utils::generate_uuid();
    // UUID v4 format: 8-4-4-4-12 = 36 chars
    REQUIRE(uuid.size() == 36);
    CHECK(uuid[8] == '-');
    CHECK(uuid[13] == '-');
    CHECK(uuid[18] == '-');
    CHECK(uuid[23] == '-');
}

TEST_CASE("trim removes whitespace", "[utils]") {
    SECTION("leading and trailing spaces") {
        REQUIRE(openclaw::utils::trim("  hello  ") == "hello");
    }

    SECTION("leading and trailing tabs and newlines") {
        REQUIRE(openclaw::utils::trim("\t\nhello\r\n") == "hello");
    }

    SECTION("no whitespace") {
        REQUIRE(openclaw::utils::trim("hello") == "hello");
    }

    SECTION("empty string") {
        REQUIRE(openclaw::utils::trim("") == "");
    }

    SECTION("only whitespace") {
        REQUIRE(openclaw::utils::trim("   \t\n  ") == "");
    }

    SECTION("internal whitespace preserved") {
        REQUIRE(openclaw::utils::trim("  hello world  ") == "hello world");
    }
}

TEST_CASE("split divides string by delimiter", "[utils]") {
    SECTION("basic split on comma") {
        auto parts = openclaw::utils::split("a,b,c", ',');
        REQUIRE(parts.size() == 3);
        CHECK(parts[0] == "a");
        CHECK(parts[1] == "b");
        CHECK(parts[2] == "c");
    }

    SECTION("no delimiter present") {
        auto parts = openclaw::utils::split("hello", ',');
        REQUIRE(parts.size() == 1);
        CHECK(parts[0] == "hello");
    }

    SECTION("trailing delimiter") {
        auto parts = openclaw::utils::split("a,b,", ',');
        REQUIRE(parts.size() == 2);
        CHECK(parts[0] == "a");
        CHECK(parts[1] == "b");
    }

    SECTION("empty string") {
        auto parts = openclaw::utils::split("", ',');
        REQUIRE(parts.size() == 0);
    }

    SECTION("consecutive delimiters") {
        auto parts = openclaw::utils::split("a,,b", ',');
        REQUIRE(parts.size() == 3);
        CHECK(parts[0] == "a");
        CHECK(parts[1] == "");
        CHECK(parts[2] == "b");
    }
}

TEST_CASE("base64 encode and decode", "[utils]") {
    SECTION("basic round-trip") {
        std::string input = "Hello, World!";
        auto encoded = openclaw::utils::base64_encode(input);
        auto decoded = openclaw::utils::base64_decode(encoded);
        REQUIRE(decoded == input);
    }

    SECTION("known encoding") {
        CHECK(openclaw::utils::base64_encode("Man") == "TWFu");
        CHECK(openclaw::utils::base64_encode("Ma") == "TWE=");
        CHECK(openclaw::utils::base64_encode("M") == "TQ==");
    }

    SECTION("empty string") {
        CHECK(openclaw::utils::base64_encode("") == "");
        CHECK(openclaw::utils::base64_decode("") == "");
    }

    SECTION("binary data round-trip") {
        std::string binary = "\x00\x01\x02\xFF\xFE";
        binary.resize(5);  // ensure embedded nulls are preserved
        auto encoded = openclaw::utils::base64_encode(binary);
        auto decoded = openclaw::utils::base64_decode(encoded);
        REQUIRE(decoded == binary);
    }
}

TEST_CASE("sha256 produces correct hash", "[utils]") {
    SECTION("known hash") {
        // SHA256 of empty string
        auto hash = openclaw::utils::sha256("");
        CHECK(hash == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    }

    SECTION("hash length") {
        auto hash = openclaw::utils::sha256("hello");
        CHECK(hash.size() == 64);  // 32 bytes as hex
    }

    SECTION("deterministic") {
        auto h1 = openclaw::utils::sha256("test data");
        auto h2 = openclaw::utils::sha256("test data");
        CHECK(h1 == h2);
    }

    SECTION("different inputs produce different hashes") {
        auto h1 = openclaw::utils::sha256("abc");
        auto h2 = openclaw::utils::sha256("abd");
        CHECK(h1 != h2);
    }
}

TEST_CASE("url_encode and url_decode", "[utils]") {
    SECTION("basic round-trip") {
        std::string input = "hello world";
        auto encoded = openclaw::utils::url_encode(input);
        auto decoded = openclaw::utils::url_decode(encoded);
        CHECK(decoded == input);
    }

    SECTION("special characters are encoded") {
        auto encoded = openclaw::utils::url_encode("a b&c=d");
        CHECK(encoded.find(' ') == std::string::npos);
        CHECK(encoded.find('&') == std::string::npos);
        CHECK(encoded.find('=') == std::string::npos);
    }

    SECTION("unreserved characters are not encoded") {
        std::string unreserved = "abcXYZ012-_.~";
        auto encoded = openclaw::utils::url_encode(unreserved);
        CHECK(encoded == unreserved);
    }

    SECTION("plus sign decoded as space") {
        CHECK(openclaw::utils::url_decode("hello+world") == "hello world");
    }

    SECTION("percent-encoded hex decoded") {
        CHECK(openclaw::utils::url_decode("hello%20world") == "hello world");
    }

    SECTION("empty string") {
        CHECK(openclaw::utils::url_encode("") == "");
        CHECK(openclaw::utils::url_decode("") == "");
    }
}

TEST_CASE("to_lower and to_upper", "[utils]") {
    CHECK(openclaw::utils::to_lower("HELLO") == "hello");
    CHECK(openclaw::utils::to_lower("Hello World") == "hello world");
    CHECK(openclaw::utils::to_lower("already") == "already");
    CHECK(openclaw::utils::to_lower("") == "");

    CHECK(openclaw::utils::to_upper("hello") == "HELLO");
    CHECK(openclaw::utils::to_upper("Hello World") == "HELLO WORLD");
    CHECK(openclaw::utils::to_upper("ALREADY") == "ALREADY");
    CHECK(openclaw::utils::to_upper("") == "");
}

TEST_CASE("starts_with and ends_with", "[utils]") {
    CHECK(openclaw::utils::starts_with("hello world", "hello"));
    CHECK_FALSE(openclaw::utils::starts_with("hello world", "world"));
    CHECK(openclaw::utils::starts_with("", ""));
    CHECK_FALSE(openclaw::utils::starts_with("", "a"));

    CHECK(openclaw::utils::ends_with("hello world", "world"));
    CHECK_FALSE(openclaw::utils::ends_with("hello world", "hello"));
    CHECK(openclaw::utils::ends_with("", ""));
    CHECK_FALSE(openclaw::utils::ends_with("", "a"));
}

TEST_CASE("timestamp_ms returns positive value", "[utils]") {
    auto ts = openclaw::utils::timestamp_ms();
    CHECK(ts > 0);
    // Should be after 2024-01-01 (1704067200000 ms)
    CHECK(ts > 1704067200000);
}

TEST_CASE("timestamp_iso returns ISO format", "[utils]") {
    auto ts = openclaw::utils::timestamp_iso();
    // Expect format like 2024-01-15T10:30:00Z
    REQUIRE(ts.size() >= 19);
    CHECK(ts[4] == '-');
    CHECK(ts[7] == '-');
    CHECK(ts[10] == 'T');
    CHECK(ts.back() == 'Z');
}
