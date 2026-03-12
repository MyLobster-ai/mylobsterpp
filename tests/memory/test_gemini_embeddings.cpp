#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>

#include "openclaw/memory/embeddings.hpp"

using namespace openclaw::memory;

// ---------------------------------------------------------------------------
// GeminiEmbeddings::normalize() static method
// ---------------------------------------------------------------------------

TEST_CASE("GeminiEmbeddings::normalize produces unit vector", "[memory][gemini]") {
    std::vector<float> vec = {3.0f, 4.0f};
    GeminiEmbeddings::normalize(vec);

    // After normalization, magnitude should be ~1.0
    float magnitude = 0.0f;
    for (float v : vec) magnitude += v * v;
    magnitude = std::sqrt(magnitude);

    CHECK_THAT(magnitude, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
    // [3, 4] -> [0.6, 0.8]
    CHECK_THAT(vec[0], Catch::Matchers::WithinAbs(0.6f, 1e-5f));
    CHECK_THAT(vec[1], Catch::Matchers::WithinAbs(0.8f, 1e-5f));
}

TEST_CASE("GeminiEmbeddings::normalize handles zero vector", "[memory][gemini]") {
    std::vector<float> vec = {0.0f, 0.0f, 0.0f};
    GeminiEmbeddings::normalize(vec);
    // Should not crash; all zeros remain zeros
    CHECK(vec[0] == 0.0f);
    CHECK(vec[1] == 0.0f);
    CHECK(vec[2] == 0.0f);
}

TEST_CASE("GeminiEmbeddings::normalize handles single element", "[memory][gemini]") {
    std::vector<float> vec = {5.0f};
    GeminiEmbeddings::normalize(vec);
    CHECK_THAT(vec[0], Catch::Matchers::WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("GeminiEmbeddings::normalize handles negative values", "[memory][gemini]") {
    std::vector<float> vec = {-3.0f, 4.0f};
    GeminiEmbeddings::normalize(vec);

    float magnitude = 0.0f;
    for (float v : vec) magnitude += v * v;
    magnitude = std::sqrt(magnitude);
    CHECK_THAT(magnitude, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
    CHECK_THAT(vec[0], Catch::Matchers::WithinAbs(-0.6f, 1e-5f));
    CHECK_THAT(vec[1], Catch::Matchers::WithinAbs(0.8f, 1e-5f));
}

TEST_CASE("GeminiEmbeddings::normalize large dimension vector", "[memory][gemini]") {
    std::vector<float> vec(768, 1.0f);
    GeminiEmbeddings::normalize(vec);

    float magnitude = 0.0f;
    for (float v : vec) magnitude += v * v;
    magnitude = std::sqrt(magnitude);
    CHECK_THAT(magnitude, Catch::Matchers::WithinAbs(1.0f, 1e-4f));

    // Each component should be 1/sqrt(768)
    float expected = 1.0f / std::sqrt(768.0f);
    CHECK_THAT(vec[0], Catch::Matchers::WithinAbs(expected, 1e-5f));
    CHECK_THAT(vec[767], Catch::Matchers::WithinAbs(expected, 1e-5f));
}

TEST_CASE("GeminiEmbeddings::normalize is idempotent", "[memory][gemini]") {
    std::vector<float> vec = {1.0f, 2.0f, 3.0f};
    GeminiEmbeddings::normalize(vec);
    auto first_pass = vec;
    GeminiEmbeddings::normalize(vec);
    // Second normalization should not change values
    for (size_t i = 0; i < vec.size(); ++i) {
        CHECK_THAT(vec[i], Catch::Matchers::WithinAbs(first_pass[i], 1e-6f));
    }
}

// ---------------------------------------------------------------------------
// GeminiEmbeddings construction and dimensions
// ---------------------------------------------------------------------------

TEST_CASE("GeminiEmbeddings dimensions returns configured value", "[memory][gemini]") {
    boost::asio::io_context ioc;
    GeminiEmbeddings embeddings(ioc, "test-key", "gemini-embedding-2-preview",
                                 "https://generativelanguage.googleapis.com", 256);
    CHECK(embeddings.dimensions() == 256);
}

TEST_CASE("GeminiEmbeddings default dimensions", "[memory][gemini]") {
    boost::asio::io_context ioc;
    GeminiEmbeddings embeddings(ioc, "test-key");
    CHECK(embeddings.dimensions() == 768);
}

TEST_CASE("GeminiEmbeddings is move-constructible", "[memory][gemini]") {
    boost::asio::io_context ioc;
    GeminiEmbeddings e1(ioc, "test-key", "gemini-embedding-2-preview",
                         "https://generativelanguage.googleapis.com", 512);
    auto e2 = std::move(e1);
    CHECK(e2.dimensions() == 512);
}

// ---------------------------------------------------------------------------
// Embedding provider chain with GeminiEmbeddings
// ---------------------------------------------------------------------------

TEST_CASE("EmbeddingProviderChain includes GeminiEmbeddings", "[memory][gemini]") {
    boost::asio::io_context ioc;
    EmbeddingProviderChain chain;
    chain.add(std::make_unique<GeminiEmbeddings>(ioc, "key1", "gemini-embedding-2-preview",
                                                   "https://api.example.com", 256));
    CHECK(chain.size() == 1);
    CHECK(chain.dimensions() == 256);
}
