#include "openclaw/infra/dotenv.hpp"
#include "openclaw/core/logger.hpp"

#include <fstream>
#include <string_view>

namespace openclaw::infra {

namespace {

auto trim(std::string_view s) -> std::string_view {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

auto parse_quoted_value(std::string_view line, char quote_char)
    -> std::pair<std::string, std::size_t> {
    std::string value;
    std::size_t pos = 0;

    while (pos < line.size()) {
        char c = line[pos];

        if (c == quote_char) {
            // End of quoted string
            return {value, pos + 1};
        }

        if (c == '\\' && quote_char == '"' && pos + 1 < line.size()) {
            // Handle escape sequences in double-quoted strings
            ++pos;
            char next = line[pos];
            switch (next) {
                case 'n':  value += '\n'; break;
                case 'r':  value += '\r'; break;
                case 't':  value += '\t'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                default:
                    value += '\\';
                    value += next;
                    break;
            }
        } else {
            value += c;
        }
        ++pos;
    }

    // Unterminated quote - return what we have
    return {value, pos};
}

} // anonymous namespace

auto parse(const std::filesystem::path& path)
    -> std::unordered_map<std::string, std::string> {
    std::unordered_map<std::string, std::string> env_map;

    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN("Could not open .env file: {}", path.string());
        return env_map;
    }

    std::string raw_line;
    int line_number = 0;

    while (std::getline(file, raw_line)) {
        ++line_number;

        auto line = trim(raw_line);

        // Skip empty lines and comments
        if (line.empty() || line.front() == '#') {
            continue;
        }

        // Strip optional "export " prefix
        if (line.starts_with("export ")) {
            line = trim(line.substr(7));
        }

        // Find the '=' delimiter
        auto eq_pos = line.find('=');
        if (eq_pos == std::string_view::npos) {
            LOG_WARN("{}:{}: Skipping malformed line (no '=')", path.string(), line_number);
            continue;
        }

        auto key = std::string(trim(line.substr(0, eq_pos)));
        if (key.empty()) {
            LOG_WARN("{}:{}: Skipping line with empty key", path.string(), line_number);
            continue;
        }

        auto value_part = line.substr(eq_pos + 1);
        auto value_trimmed = trim(value_part);

        std::string value;

        if (!value_trimmed.empty() &&
            (value_trimmed.front() == '"' || value_trimmed.front() == '\'')) {
            // Quoted value
            char quote = value_trimmed.front();
            auto [parsed, consumed] = parse_quoted_value(
                value_trimmed.substr(1), quote);
            value = std::move(parsed);
        } else {
            // Unquoted value: strip inline comments
            auto str = std::string(value_trimmed);
            auto comment_pos = str.find(" #");
            if (comment_pos != std::string::npos) {
                str = str.substr(0, comment_pos);
            }
            // Trim trailing whitespace
            auto end = str.find_last_not_of(" \t");
            if (end != std::string::npos) {
                str = str.substr(0, end + 1);
            }
            value = std::move(str);
        }

        env_map[std::move(key)] = std::move(value);
    }

    LOG_DEBUG("Parsed {} variables from {}", env_map.size(), path.string());
    return env_map;
}

void load(const std::filesystem::path& path, bool overwrite) {
    auto env_map = parse(path);

    for (const auto& [key, value] : env_map) {
        if (!overwrite && std::getenv(key.c_str()) != nullptr) {
            LOG_TRACE("Skipping existing env var: {}", key);
            continue;
        }

        if (::setenv(key.c_str(), value.c_str(), overwrite ? 1 : 0) != 0) {
            LOG_WARN("Failed to set env var: {}", key);
        }
    }

    LOG_INFO("Loaded .env from {}", path.string());
}

} // namespace openclaw::infra
