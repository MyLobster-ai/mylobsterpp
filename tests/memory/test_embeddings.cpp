#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <numeric>
#include <vector>

#include "openclaw/memory/embeddings.hpp"
#include "openclaw/memory/vector_store.hpp"

using json = nlohmann::json;

// Helper: compute cosine similarity between two float vectors
static double cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0;

    double dot = 0.0, norm_a = 0.0, norm_b = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * static_cast<double>(b[i]);
        norm_a += static_cast<double>(a[i]) * static_cast<double>(a[i]);
        norm_b += static_cast<double>(b[i]) * static_cast<double>(b[i]);
    }

    double denom = std::sqrt(norm_a) * std::sqrt(norm_b);
    if (denom == 0.0) return 0.0;
    return dot / denom;
}

// Helper: L2 normalize a vector
static std::vector<float> normalize(const std::vector<float>& v) {
    double norm = 0.0;
    for (float x : v) norm += static_cast<double>(x) * static_cast<double>(x);
    norm = std::sqrt(norm);
    if (norm == 0.0) return v;

    std::vector<float> result(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        result[i] = static_cast<float>(static_cast<double>(v[i]) / norm);
    }
    return result;
}

TEST_CASE("Cosine similarity of identical vectors is 1.0", "[memory][embeddings]") {
    std::vector<float> v = {1.0f, 2.0f, 3.0f, 4.0f};
    double sim = cosine_similarity(v, v);
    CHECK(sim == Catch::Approx(1.0).margin(1e-6));
}

TEST_CASE("Cosine similarity of orthogonal vectors is 0.0", "[memory][embeddings]") {
    std::vector<float> a = {1.0f, 0.0f, 0.0f};
    std::vector<float> b = {0.0f, 1.0f, 0.0f};
    double sim = cosine_similarity(a, b);
    CHECK(sim == Catch::Approx(0.0).margin(1e-6));
}

TEST_CASE("Cosine similarity of opposite vectors is -1.0", "[memory][embeddings]") {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {-1.0f, -2.0f, -3.0f};
    double sim = cosine_similarity(a, b);
    CHECK(sim == Catch::Approx(-1.0).margin(1e-6));
}

TEST_CASE("Vector normalization produces unit vector", "[memory][embeddings]") {
    std::vector<float> v = {3.0f, 4.0f};
    auto normed = normalize(v);

    REQUIRE(normed.size() == 2);
    CHECK(normed[0] == Catch::Approx(0.6f).margin(1e-5f));
    CHECK(normed[1] == Catch::Approx(0.8f).margin(1e-5f));

    // Magnitude should be 1.0
    float mag = 0.0f;
    for (float x : normed) mag += x * x;
    CHECK(std::sqrt(mag) == Catch::Approx(1.0f).margin(1e-5f));
}

TEST_CASE("Normalize zero vector returns zero vector", "[memory][embeddings]") {
    std::vector<float> v = {0.0f, 0.0f, 0.0f};
    auto normed = normalize(v);
    for (float x : normed) {
        CHECK(x == 0.0f);
    }
}

TEST_CASE("VectorEntry serialization", "[memory][embeddings]") {
    openclaw::memory::VectorEntry entry{
        .id = "vec-001",
        .embedding = {0.1f, 0.2f, 0.3f},
        .content = "test content",
        .metadata = json{{"source", "unit_test"}},
        .score = 0.95,
    };

    json j;
    openclaw::memory::to_json(j, entry);

    CHECK(j["id"] == "vec-001");
    CHECK(j["content"] == "test content");
    CHECK(j["metadata"]["source"] == "unit_test");

    openclaw::memory::VectorEntry restored;
    openclaw::memory::from_json(j, restored);

    CHECK(restored.id == "vec-001");
    CHECK(restored.content == "test content");
}

TEST_CASE("Cosine similarity with empty vectors", "[memory][embeddings]") {
    std::vector<float> empty;
    std::vector<float> v = {1.0f, 2.0f};

    CHECK(cosine_similarity(empty, empty) == 0.0);
    CHECK(cosine_similarity(empty, v) == 0.0);
}
