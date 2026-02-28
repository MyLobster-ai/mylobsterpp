#include "openclaw/core/utils.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

#include <openssl/evp.h>
#include <uuid.h>

namespace openclaw::utils {

auto generate_id(std::size_t length) -> std::string {
    static constexpr std::string_view chars =
        "abcdefghijklmnopqrstuvwxyz0123456789";
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, chars.size() - 1);

    std::string result;
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        result += chars[dist(rng)];
    }
    return result;
}

auto generate_uuid() -> std::string {
    static thread_local std::mt19937 rng(std::random_device{}());
    auto gen = uuids::uuid_random_generator(rng);
    return uuids::to_string(gen());
}

auto timestamp_ms() -> int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

auto timestamp_iso() -> std::string {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time), "%FT%TZ");
    return oss.str();
}

auto trim(std::string_view s) -> std::string {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string_view::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return std::string(s.substr(start, end - start + 1));
}

auto split(std::string_view s, char delim) -> std::vector<std::string> {
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < s.size()) {
        auto next = s.find(delim, pos);
        if (next == std::string_view::npos) {
            parts.emplace_back(s.substr(pos));
            break;
        }
        parts.emplace_back(s.substr(pos, next - pos));
        pos = next + 1;
    }
    return parts;
}

auto to_lower(std::string_view s) -> std::string {
    std::string result(s);
    std::ranges::transform(result, result.begin(), ::tolower);
    return result;
}

auto to_upper(std::string_view s) -> std::string {
    std::string result(s);
    std::ranges::transform(result, result.begin(), ::toupper);
    return result;
}

auto starts_with(std::string_view s, std::string_view prefix) -> bool {
    return s.starts_with(prefix);
}

auto ends_with(std::string_view s, std::string_view suffix) -> bool {
    return s.ends_with(suffix);
}

auto base64_encode(std::string_view data) -> std::string {
    static constexpr std::string_view table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < data.size()) {
        auto a = static_cast<uint8_t>(data[i++]);
        auto b = static_cast<uint8_t>(data[i++]);
        auto c = static_cast<uint8_t>(data[i++]);
        result += table[(a >> 2) & 0x3F];
        result += table[((a & 0x03) << 4) | ((b >> 4) & 0x0F)];
        result += table[((b & 0x0F) << 2) | ((c >> 6) & 0x03)];
        result += table[c & 0x3F];
    }
    if (i < data.size()) {
        auto a = static_cast<uint8_t>(data[i++]);
        result += table[(a >> 2) & 0x3F];
        if (i < data.size()) {
            auto b = static_cast<uint8_t>(data[i]);
            result += table[((a & 0x03) << 4) | ((b >> 4) & 0x0F)];
            result += table[((b & 0x0F) << 2)];
        } else {
            result += table[(a & 0x03) << 4];
            result += '=';
        }
        result += '=';
    }
    return result;
}

namespace {
constexpr auto make_b64_decode_table() -> std::array<uint8_t, 256> {
    std::array<uint8_t, 256> t{};
    for (int i = 0; i < 26; ++i) t['A' + i] = static_cast<uint8_t>(i);
    for (int i = 0; i < 26; ++i) t['a' + i] = static_cast<uint8_t>(26 + i);
    for (int i = 0; i < 10; ++i) t['0' + i] = static_cast<uint8_t>(52 + i);
    t['+'] = 62;
    t['/'] = 63;
    return t;
}
} // namespace

auto base64_decode(std::string_view data) -> std::string {
    static constexpr auto table = make_b64_decode_table();

    std::string result;
    result.reserve((data.size() / 4) * 3);

    uint32_t buf = 0;
    int bits = 0;
    for (char c : data) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        buf = (buf << 6) | table[static_cast<uint8_t>(c)];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result += static_cast<char>((buf >> bits) & 0xFF);
        }
    }
    return result;
}

auto sha256(std::string_view data) -> std::string {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < hash_len; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(hash[i]);
    }
    return oss.str();
}

auto url_encode(std::string_view s) -> std::string {
    std::ostringstream oss;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else {
            oss << '%' << std::hex << std::uppercase << std::setfill('0')
                << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    return oss.str();
}

auto url_decode(std::string_view s) -> std::string {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hi = s[i + 1];
            auto lo = s[i + 2];
            auto hex_to_int = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            result += static_cast<char>((hex_to_int(hi) << 4) | hex_to_int(lo));
            i += 2;
        } else if (s[i] == '+') {
            result += ' ';
        } else {
            result += s[i];
        }
    }
    return result;
}

auto normalize_session_key(std::string_view key, std::string_view agent_id) -> std::string {
    if (key.empty()) return std::string(key);

    std::string prefix = std::string(agent_id) + ":";

    // Strip existing prefix if present to prevent doubling
    std::string_view stripped = key;
    if (key.starts_with(prefix)) {
        stripped = key.substr(prefix.size());
    }

    // Re-prefix
    return prefix + std::string(stripped);
}

} // namespace openclaw::utils
