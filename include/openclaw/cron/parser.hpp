#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "openclaw/core/error.hpp"
#include "openclaw/core/types.hpp"

namespace openclaw::cron {

/// Parsed representation of a standard 5-field cron expression.
///
/// Each field is a sorted, deduplicated vector of the matching values.
/// Supports: single values, ranges (1-5), steps (*/5, 1-10/2),
/// lists (1,3,5), wildcards (*), and named days/months.
struct CronExpression {
    std::vector<int> minutes;    // 0-59
    std::vector<int> hours;      // 0-23
    std::vector<int> days;       // 1-31  (day of month)
    std::vector<int> months;     // 1-12
    std::vector<int> weekdays;   // 0-6   (0 = Sunday)
};

/// Parse a standard 5-field cron expression string.
///
/// Supported syntax per field:
///   - `*`          all values in the field's range
///   - `N`          single value
///   - `N-M`        range from N to M inclusive
///   - `N-M/S`      range with step S
///   - `*/S`        full range with step S
///   - `N,M,O`      list of values (each element may be a range or step)
///
/// Month names (jan-dec) and day names (sun-sat) are accepted (case-insensitive).
///
/// @param expr  The cron expression, e.g. "0 */2 * * 1-5".
/// @returns     Parsed CronExpression on success, Error on invalid input.
auto parse_cron(std::string_view expr) -> Result<CronExpression>;

/// Compute the next occurrence at or after `from` that matches `expr`.
///
/// Searches forward through time, advancing minute-by-minute up to
/// a maximum horizon (4 years) before giving up.
///
/// @param expr  A parsed cron expression.
/// @param from  The starting timestamp.
/// @returns     The next matching timestamp.
auto next_occurrence(const CronExpression& expr, Timestamp from) -> Timestamp;

/// Check whether a given timestamp matches the cron expression.
///
/// Matching is performed at minute granularity: the seconds and
/// sub-second components of `t` are ignored.
///
/// @param expr  A parsed cron expression.
/// @param t     The timestamp to check.
/// @returns     True if `t` matches all fields of `expr`.
auto matches(const CronExpression& expr, Timestamp t) -> bool;

} // namespace openclaw::cron
