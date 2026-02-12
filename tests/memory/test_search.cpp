#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "openclaw/core/utils.hpp"
#include "openclaw/memory/vector_store.hpp"

using json = nlohmann::json;

// Simple search query structure for testing parsing logic.
// Since the codebase may not expose a dedicated search query parser,
// we test the patterns that would be used for parsing search queries.
struct SearchQuery {
    std::string text;
    size_t limit = 10;
    double min_score = 0.0;
    std::string filter_source;

    static SearchQuery parse(const std::string& raw) {
        SearchQuery q;
        auto parts = openclaw::utils::split(raw, ' ');

        std::vector<std::string> text_parts;
        for (const auto& part : parts) {
            if (openclaw::utils::starts_with(part, "limit:")) {
                auto val = part.substr(6);
                q.limit = std::stoul(val);
            } else if (openclaw::utils::starts_with(part, "min_score:")) {
                auto val = part.substr(10);
                q.min_score = std::stod(val);
            } else if (openclaw::utils::starts_with(part, "source:")) {
                q.filter_source = part.substr(7);
            } else {
                text_parts.push_back(part);
            }
        }

        // Join remaining parts as the text query
        for (size_t i = 0; i < text_parts.size(); ++i) {
            if (i > 0) q.text += " ";
            q.text += text_parts[i];
        }

        return q;
    }
};

TEST_CASE("SearchQuery parse plain text", "[memory][search]") {
    auto q = SearchQuery::parse("find similar documents");

    CHECK(q.text == "find similar documents");
    CHECK(q.limit == 10);
    CHECK(q.min_score == 0.0);
    CHECK(q.filter_source.empty());
}

TEST_CASE("SearchQuery parse with limit modifier", "[memory][search]") {
    auto q = SearchQuery::parse("test query limit:5");

    CHECK(q.text == "test query");
    CHECK(q.limit == 5);
}

TEST_CASE("SearchQuery parse with min_score modifier", "[memory][search]") {
    auto q = SearchQuery::parse("min_score:0.8 search term");

    CHECK(q.text == "search term");
    CHECK(q.min_score == Catch::Approx(0.8));
}

TEST_CASE("SearchQuery parse with source filter", "[memory][search]") {
    auto q = SearchQuery::parse("source:email hello world limit:3");

    CHECK(q.text == "hello world");
    CHECK(q.limit == 3);
    CHECK(q.filter_source == "email");
}

TEST_CASE("SearchQuery parse empty input", "[memory][search]") {
    auto q = SearchQuery::parse("");

    CHECK(q.text == "");
    CHECK(q.limit == 10);
}

TEST_CASE("VectorEntry metadata filtering", "[memory][search]") {
    // Simulate a set of search results and filter by metadata
    std::vector<openclaw::memory::VectorEntry> results = {
        {.id = "1", .embedding = {}, .content = "email content",
         .metadata = json{{"source", "email"}}, .score = 0.9},
        {.id = "2", .embedding = {}, .content = "chat content",
         .metadata = json{{"source", "chat"}}, .score = 0.85},
        {.id = "3", .embedding = {}, .content = "another email",
         .metadata = json{{"source", "email"}}, .score = 0.7},
    };

    SECTION("filter by source") {
        std::vector<openclaw::memory::VectorEntry> filtered;
        std::copy_if(results.begin(), results.end(), std::back_inserter(filtered),
            [](const auto& e) {
                return e.metadata.contains("source") &&
                       e.metadata["source"] == "email";
            });

        REQUIRE(filtered.size() == 2);
        CHECK(filtered[0].id == "1");
        CHECK(filtered[1].id == "3");
    }

    SECTION("filter by minimum score") {
        double min_score = 0.8;
        std::vector<openclaw::memory::VectorEntry> filtered;
        std::copy_if(results.begin(), results.end(), std::back_inserter(filtered),
            [min_score](const auto& e) { return e.score >= min_score; });

        REQUIRE(filtered.size() == 2);
        CHECK(filtered[0].score >= 0.8);
        CHECK(filtered[1].score >= 0.8);
    }

    SECTION("sort by score descending") {
        auto sorted = results;
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.score > b.score; });

        CHECK(sorted[0].id == "1");
        CHECK(sorted[1].id == "2");
        CHECK(sorted[2].id == "3");
    }
}
