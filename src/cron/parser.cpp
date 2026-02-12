#include "openclaw/cron/parser.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <algorithm>
#include <charconv>
#include <ctime>
#include <sstream>
#include <unordered_map>

namespace openclaw::cron {

namespace {

// ---------------------------------------------------------------------------
// Named value maps
// ---------------------------------------------------------------------------

const std::unordered_map<std::string, int> kMonthNames = {
    {"jan", 1}, {"feb", 2},  {"mar", 3},  {"apr", 4},
    {"may", 5}, {"jun", 6},  {"jul", 7},  {"aug", 8},
    {"sep", 9}, {"oct", 10}, {"nov", 11}, {"dec", 12},
};

const std::unordered_map<std::string, int> kDayNames = {
    {"sun", 0}, {"mon", 1}, {"tue", 2}, {"wed", 3},
    {"thu", 4}, {"fri", 5}, {"sat", 6},
};

/// Try to resolve a token to an integer, checking named values first.
auto resolve_value(std::string_view token, int min_val, int max_val,
                   const std::unordered_map<std::string, int>* names)
    -> Result<int>
{
    // Try named lookup (case-insensitive).
    if (names && token.size() >= 3) {
        auto lower = utils::to_lower(token);
        if (auto it = names->find(lower); it != names->end()) {
            return it->second;
        }
    }

    // Parse as integer.
    int value = 0;
    auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec != std::errc{} || ptr != token.data() + token.size()) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Invalid cron value",
            std::string(token)));
    }

    if (value < min_val || value > max_val) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Cron value out of range",
            std::string(token) + " (expected " +
                std::to_string(min_val) + "-" + std::to_string(max_val) + ")"));
    }

    return value;
}

/// Parse a single field element, which may be:
///   *          -> full range
///   N          -> single value
///   N-M        -> range
///   N-M/S      -> range with step
///   */S        -> full range with step
///   name       -> named month or weekday
auto parse_element(std::string_view elem, int min_val, int max_val,
                   const std::unordered_map<std::string, int>* names)
    -> Result<std::vector<int>>
{
    std::vector<int> values;

    // Check for step: split on '/'.
    int step = 1;
    std::string_view range_part = elem;

    if (auto slash = elem.find('/'); slash != std::string_view::npos) {
        range_part = elem.substr(0, slash);
        auto step_part = elem.substr(slash + 1);

        int s = 0;
        auto [ptr, ec] = std::from_chars(step_part.data(),
                                          step_part.data() + step_part.size(), s);
        if (ec != std::errc{} || ptr != step_part.data() + step_part.size() || s <= 0) {
            return std::unexpected(make_error(
                ErrorCode::InvalidArgument,
                "Invalid step value",
                std::string(step_part)));
        }
        step = s;
    }

    // Wildcard: * or */S
    if (range_part == "*") {
        for (int i = min_val; i <= max_val; i += step) {
            values.push_back(i);
        }
        return values;
    }

    // Range: N-M or N-M/S
    if (auto dash = range_part.find('-'); dash != std::string_view::npos) {
        auto start_str = range_part.substr(0, dash);
        auto end_str = range_part.substr(dash + 1);

        auto start_result = resolve_value(start_str, min_val, max_val, names);
        if (!start_result) return std::unexpected(start_result.error());

        auto end_result = resolve_value(end_str, min_val, max_val, names);
        if (!end_result) return std::unexpected(end_result.error());

        int start = *start_result;
        int end = *end_result;

        if (start > end) {
            return std::unexpected(make_error(
                ErrorCode::InvalidArgument,
                "Invalid range",
                std::string(start_str) + "-" + std::string(end_str)));
        }

        for (int i = start; i <= end; i += step) {
            values.push_back(i);
        }
        return values;
    }

    // Single value (possibly a name).
    auto val = resolve_value(range_part, min_val, max_val, names);
    if (!val) return std::unexpected(val.error());

    // If a step was specified with a single value, treat it as range from value to max.
    if (step > 1) {
        for (int i = *val; i <= max_val; i += step) {
            values.push_back(i);
        }
    } else {
        values.push_back(*val);
    }

    return values;
}

/// Parse a complete cron field, which may be a comma-separated list of elements.
auto parse_field(std::string_view field, int min_val, int max_val,
                 const std::unordered_map<std::string, int>* names = nullptr)
    -> Result<std::vector<int>>
{
    std::vector<int> result;

    // Split on commas.
    size_t start = 0;
    while (start < field.size()) {
        auto comma = field.find(',', start);
        auto part = field.substr(start, comma == std::string_view::npos
                                            ? std::string_view::npos
                                            : comma - start);

        auto elem_result = parse_element(part, min_val, max_val, names);
        if (!elem_result) return std::unexpected(elem_result.error());

        for (int v : *elem_result) {
            result.push_back(v);
        }

        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }

    // Sort and deduplicate.
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());

    return result;
}

/// Split a string by whitespace, returning non-empty tokens.
auto split_whitespace(std::string_view s) -> std::vector<std::string_view> {
    std::vector<std::string_view> tokens;
    size_t i = 0;
    while (i < s.size()) {
        // Skip whitespace.
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        if (i >= s.size()) break;

        size_t start = i;
        while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
        tokens.push_back(s.substr(start, i - start));
    }
    return tokens;
}

/// Convert a Timestamp to a std::tm in UTC.
auto to_tm(Timestamp ts) -> std::tm {
    auto time_t_val = Clock::to_time_t(ts);
    std::tm tm_val{};
#ifdef _WIN32
    gmtime_s(&tm_val, &time_t_val);
#else
    gmtime_r(&time_t_val, &tm_val);
#endif
    return tm_val;
}

/// Convert a std::tm in UTC to a Timestamp.
auto from_tm(std::tm& tm_val) -> Timestamp {
#ifdef _WIN32
    auto time_t_val = _mkgmtime(&tm_val);
#else
    auto time_t_val = timegm(&tm_val);
#endif
    return Clock::from_time_t(time_t_val);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

auto parse_cron(std::string_view expr) -> Result<CronExpression> {
    auto fields = split_whitespace(expr);

    if (fields.size() != 5) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Cron expression must have exactly 5 fields",
            "got " + std::to_string(fields.size()) + " in '" +
                std::string(expr) + "'"));
    }

    auto minutes = parse_field(fields[0], 0, 59);
    if (!minutes) return std::unexpected(minutes.error());

    auto hours = parse_field(fields[1], 0, 23);
    if (!hours) return std::unexpected(hours.error());

    auto days = parse_field(fields[2], 1, 31);
    if (!days) return std::unexpected(days.error());

    auto months_result = parse_field(fields[3], 1, 12, &kMonthNames);
    if (!months_result) return std::unexpected(months_result.error());

    auto weekdays = parse_field(fields[4], 0, 6, &kDayNames);
    if (!weekdays) return std::unexpected(weekdays.error());

    return CronExpression{
        .minutes = std::move(*minutes),
        .hours = std::move(*hours),
        .days = std::move(*days),
        .months = std::move(*months_result),
        .weekdays = std::move(*weekdays),
    };
}

auto matches(const CronExpression& expr, Timestamp t) -> bool {
    auto tm = to_tm(t);

    int minute = tm.tm_min;
    int hour = tm.tm_hour;
    int day = tm.tm_mday;
    int month = tm.tm_mon + 1;   // tm_mon is 0-based
    int weekday = tm.tm_wday;    // tm_wday: 0 = Sunday

    auto contains = [](const std::vector<int>& v, int val) -> bool {
        return std::binary_search(v.begin(), v.end(), val);
    };

    return contains(expr.minutes, minute) &&
           contains(expr.hours, hour) &&
           contains(expr.days, day) &&
           contains(expr.months, month) &&
           contains(expr.weekdays, weekday);
}

auto next_occurrence(const CronExpression& expr, Timestamp from) -> Timestamp {
    // Truncate to the start of the current minute and advance by one minute.
    auto time_t_val = Clock::to_time_t(from);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time_t_val);
#else
    gmtime_r(&time_t_val, &tm);
#endif
    tm.tm_sec = 0;

    // Start from the next minute.
    tm.tm_min += 1;
    auto candidate = from_tm(tm);

    // Search forward up to ~4 years (2,103,840 minutes).
    constexpr int kMaxIterations = 4 * 366 * 24 * 60;

    for (int i = 0; i < kMaxIterations; ++i) {
        if (matches(expr, candidate)) {
            return candidate;
        }
        // Advance by one minute.
        candidate += std::chrono::minutes(1);
    }

    // Should not happen for valid cron expressions, but return a far-future
    // sentinel to avoid undefined behavior.
    LOG_WARN("next_occurrence exceeded search horizon for cron expression");
    return candidate;
}

} // namespace openclaw::cron
