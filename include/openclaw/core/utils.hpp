#pragma once

#include <chrono>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace openclaw::utils {

auto generate_id(std::size_t length = 16) -> std::string;
auto generate_uuid() -> std::string;
auto timestamp_ms() -> int64_t;
auto timestamp_iso() -> std::string;
auto trim(std::string_view s) -> std::string;
auto split(std::string_view s, char delim) -> std::vector<std::string>;
auto to_lower(std::string_view s) -> std::string;
auto to_upper(std::string_view s) -> std::string;
auto starts_with(std::string_view s, std::string_view prefix) -> bool;
auto ends_with(std::string_view s, std::string_view suffix) -> bool;
auto base64_encode(std::string_view data) -> std::string;
auto base64_decode(std::string_view data) -> std::string;
auto sha256(std::string_view data) -> std::string;
auto url_encode(std::string_view s) -> std::string;
auto url_decode(std::string_view s) -> std::string;

} // namespace openclaw::utils
