#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_set>

// Replicate the stop-word filtering logic for testing
namespace {
const std::unordered_set<std::string>& stop_words() {
    static const std::unordered_set<std::string> words = {
        "the", "a", "an", "is", "are", "of", "in", "for", "on", "with",
        "to", "and", "or", "but", "not", "this", "that",
        "el", "la", "los", "las", "de", "en",
        "o", "os", "um", "uma", "do", "da",
    };
    return words;
}

auto filter_stop_words(std::string_view query) -> std::string {
    std::string result;
    std::istringstream stream{std::string{query}};
    std::string word;
    bool first = true;
    while (stream >> word) {
        std::string lower = word;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (stop_words().contains(lower)) continue;
        if (!first) result += ' ';
        result += word;
        first = false;
    }
    return result.empty() ? std::string(query) : result;
}
} // anonymous namespace

TEST_CASE("Multi-language stop-word filtering", "[memory][stopwords]") {
    SECTION("Filters English stop words") {
        auto result = filter_stop_words("the quick brown fox is a jumper");
        CHECK(result.find("the") == std::string::npos);
        CHECK(result.find("quick") != std::string::npos);
        CHECK(result.find("brown") != std::string::npos);
        CHECK(result.find("fox") != std::string::npos);
    }
    SECTION("Filters Spanish stop words") {
        auto result = filter_stop_words("el gato en la casa");
        CHECK(result.find("gato") != std::string::npos);
        CHECK(result.find("casa") != std::string::npos);
    }
    SECTION("Filters Portuguese stop words") {
        auto result = filter_stop_words("o gato da casa");
        CHECK(result.find("gato") != std::string::npos);
        CHECK(result.find("casa") != std::string::npos);
    }
    SECTION("Returns original if all words are stop words") {
        auto result = filter_stop_words("the a an");
        CHECK(!result.empty());
    }
    SECTION("Preserves non-stop words") {
        CHECK(filter_stop_words("quantum computing") == "quantum computing");
    }
}
