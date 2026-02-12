#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "openclaw/core/utils.hpp"

// Cron expression field parser for testing.
// Parses a single cron field (minute, hour, day, month, day-of-week)
// and expands it into a set of matching values.
//
// Supports:
//   *       - all values in [min, max]
//   N       - single value
//   N-M     - range from N to M inclusive
//   N-M/S   - range with step S
//   * /S    - all values with step S  (written without space)
//   N,M,O   - list of values
namespace cron {

struct ParseResult {
    std::set<int> values;
    bool valid = true;
    std::string error;
};

inline auto parse_field(std::string_view field, int min_val, int max_val) -> ParseResult {
    ParseResult result;

    if (field.empty()) {
        result.valid = false;
        result.error = "empty field";
        return result;
    }

    // Split by comma for lists
    auto parts = openclaw::utils::split(std::string(field), ',');

    for (const auto& part : parts) {
        auto trimmed = openclaw::utils::trim(part);
        if (trimmed.empty()) {
            result.valid = false;
            result.error = "empty list element";
            return result;
        }

        // Check for step: */S or N-M/S
        int step = 1;
        std::string base_str(trimmed);
        auto slash_pos = trimmed.find('/');
        if (slash_pos != std::string_view::npos) {
            auto step_str = trimmed.substr(slash_pos + 1);
            try {
                step = std::stoi(std::string(step_str));
            } catch (...) {
                result.valid = false;
                result.error = "invalid step value";
                return result;
            }
            if (step <= 0) {
                result.valid = false;
                result.error = "step must be positive";
                return result;
            }
            base_str = std::string(trimmed.substr(0, slash_pos));
        }

        if (base_str == "*") {
            // Wildcard (with optional step)
            for (int i = min_val; i <= max_val; i += step) {
                result.values.insert(i);
            }
        } else if (base_str.find('-') != std::string::npos) {
            // Range: N-M
            auto dash_pos = base_str.find('-');
            try {
                int range_start = std::stoi(base_str.substr(0, dash_pos));
                int range_end = std::stoi(base_str.substr(dash_pos + 1));

                if (range_start < min_val || range_end > max_val || range_start > range_end) {
                    result.valid = false;
                    result.error = "range out of bounds";
                    return result;
                }

                for (int i = range_start; i <= range_end; i += step) {
                    result.values.insert(i);
                }
            } catch (...) {
                result.valid = false;
                result.error = "invalid range";
                return result;
            }
        } else {
            // Single value
            try {
                int val = std::stoi(base_str);
                if (val < min_val || val > max_val) {
                    result.valid = false;
                    result.error = "value out of range";
                    return result;
                }
                result.values.insert(val);
            } catch (...) {
                result.valid = false;
                result.error = "invalid number";
                return result;
            }
        }
    }

    return result;
}

} // namespace cron

TEST_CASE("Cron wildcard (*) expands to all values", "[cron][parser]") {
    SECTION("minutes: 0-59") {
        auto result = cron::parse_field("*", 0, 59);
        REQUIRE(result.valid);
        CHECK(result.values.size() == 60);
        CHECK(result.values.count(0) == 1);
        CHECK(result.values.count(59) == 1);
    }

    SECTION("hours: 0-23") {
        auto result = cron::parse_field("*", 0, 23);
        REQUIRE(result.valid);
        CHECK(result.values.size() == 24);
    }

    SECTION("day of month: 1-31") {
        auto result = cron::parse_field("*", 1, 31);
        REQUIRE(result.valid);
        CHECK(result.values.size() == 31);
        CHECK(result.values.count(1) == 1);
        CHECK(result.values.count(31) == 1);
    }
}

TEST_CASE("Cron single value", "[cron][parser]") {
    auto result = cron::parse_field("15", 0, 59);
    REQUIRE(result.valid);
    REQUIRE(result.values.size() == 1);
    CHECK(result.values.count(15) == 1);
}

TEST_CASE("Cron single value out of range", "[cron][parser]") {
    auto result = cron::parse_field("60", 0, 59);
    CHECK_FALSE(result.valid);
}

TEST_CASE("Cron range N-M", "[cron][parser]") {
    SECTION("valid range") {
        auto result = cron::parse_field("5-10", 0, 59);
        REQUIRE(result.valid);
        CHECK(result.values.size() == 6);
        CHECK(result.values.count(5) == 1);
        CHECK(result.values.count(10) == 1);
        CHECK(result.values.count(4) == 0);
        CHECK(result.values.count(11) == 0);
    }

    SECTION("single-element range") {
        auto result = cron::parse_field("7-7", 0, 59);
        REQUIRE(result.valid);
        CHECK(result.values.size() == 1);
        CHECK(result.values.count(7) == 1);
    }

    SECTION("range out of bounds") {
        auto result = cron::parse_field("25-35", 0, 23);
        CHECK_FALSE(result.valid);
    }
}

TEST_CASE("Cron step with wildcard (*/N)", "[cron][parser]") {
    SECTION("every 15 minutes") {
        auto result = cron::parse_field("*/15", 0, 59);
        REQUIRE(result.valid);
        std::set<int> expected = {0, 15, 30, 45};
        CHECK(result.values == expected);
    }

    SECTION("every 2 hours") {
        auto result = cron::parse_field("*/2", 0, 23);
        REQUIRE(result.valid);
        CHECK(result.values.size() == 12);
        CHECK(result.values.count(0) == 1);
        CHECK(result.values.count(1) == 0);
        CHECK(result.values.count(22) == 1);
    }
}

TEST_CASE("Cron step with range (N-M/S)", "[cron][parser]") {
    auto result = cron::parse_field("1-10/3", 0, 59);
    REQUIRE(result.valid);
    std::set<int> expected = {1, 4, 7, 10};
    CHECK(result.values == expected);
}

TEST_CASE("Cron list (N,M,O)", "[cron][parser]") {
    SECTION("simple list") {
        auto result = cron::parse_field("1,15,30,45", 0, 59);
        REQUIRE(result.valid);
        std::set<int> expected = {1, 15, 30, 45};
        CHECK(result.values == expected);
    }

    SECTION("list with duplicates deduplicates") {
        auto result = cron::parse_field("5,5,10,10", 0, 59);
        REQUIRE(result.valid);
        std::set<int> expected = {5, 10};
        CHECK(result.values == expected);
    }

    SECTION("mixed list and range") {
        auto result = cron::parse_field("1,5-8,15", 0, 59);
        REQUIRE(result.valid);
        std::set<int> expected = {1, 5, 6, 7, 8, 15};
        CHECK(result.values == expected);
    }
}

TEST_CASE("Cron invalid expressions", "[cron][parser]") {
    SECTION("empty string") {
        auto result = cron::parse_field("", 0, 59);
        CHECK_FALSE(result.valid);
    }

    SECTION("non-numeric") {
        auto result = cron::parse_field("abc", 0, 59);
        CHECK_FALSE(result.valid);
    }

    SECTION("negative step") {
        auto result = cron::parse_field("*/-1", 0, 59);
        CHECK_FALSE(result.valid);
    }

    SECTION("zero step") {
        auto result = cron::parse_field("*/0", 0, 59);
        CHECK_FALSE(result.valid);
    }
}
