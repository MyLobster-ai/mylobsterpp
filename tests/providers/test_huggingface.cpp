#include <catch2/catch_test_macros.hpp>

#include "openclaw/providers/huggingface.hpp"

using namespace openclaw::providers;
using json = nlohmann::json;

TEST_CASE("HuggingFace static catalog", "[providers][huggingface]") {
    SECTION("Contains known models") {
        auto catalog = HuggingFaceProvider::static_catalog();
        REQUIRE(!catalog.empty());

        // Check for well-known models
        bool found_deepseek = false;
        bool found_qwen = false;
        for (const auto& model : catalog) {
            if (model.id.find("DeepSeek") != std::string::npos) found_deepseek = true;
            if (model.id.find("Qwen") != std::string::npos) found_qwen = true;
        }
        CHECK(found_deepseek);
        CHECK(found_qwen);
    }

    SECTION("Models have valid context lengths") {
        for (const auto& model : HuggingFaceProvider::static_catalog()) {
            CHECK(model.context_length > 0);
            CHECK(model.max_tokens > 0);
            CHECK(model.max_tokens <= model.context_length);
        }
    }
}

TEST_CASE("HuggingFace route policy suffix stripping", "[providers][huggingface]") {
    SECTION(":cheapest suffix is stripped") {
        auto [model, policy] = HuggingFaceProvider::strip_route_policy(
            "deepseek-ai/DeepSeek-R1:cheapest");
        CHECK(model == "deepseek-ai/DeepSeek-R1");
        CHECK(policy == "cheapest");
    }

    SECTION(":fastest suffix is stripped") {
        auto [model, policy] = HuggingFaceProvider::strip_route_policy(
            "Qwen/Qwen3-235B-A22B:fastest");
        CHECK(model == "Qwen/Qwen3-235B-A22B");
        CHECK(policy == "fastest");
    }

    SECTION("No suffix returns empty policy") {
        auto [model, policy] = HuggingFaceProvider::strip_route_policy(
            "meta-llama/Llama-3.3-70B-Instruct");
        CHECK(model == "meta-llama/Llama-3.3-70B-Instruct");
        CHECK(policy.empty());
    }

    SECTION("Unknown suffix is not stripped") {
        auto [model, policy] = HuggingFaceProvider::strip_route_policy(
            "model:unknown");
        CHECK(model == "model:unknown");
        CHECK(policy.empty());
    }
}

TEST_CASE("HuggingFace reasoning detection", "[providers][huggingface]") {
    CHECK(HuggingFaceProvider::is_reasoning_model("deepseek-ai/DeepSeek-R1"));
    CHECK(HuggingFaceProvider::is_reasoning_model("some-reasoning-model"));
    CHECK(HuggingFaceProvider::is_reasoning_model("org/model-thinking-v2"));
    CHECK(HuggingFaceProvider::is_reasoning_model("org/model-reason-7B"));

    CHECK_FALSE(HuggingFaceProvider::is_reasoning_model("meta-llama/Llama-3.3-70B"));
    CHECK_FALSE(HuggingFaceProvider::is_reasoning_model("Qwen/Qwen3-235B-A22B"));
    CHECK_FALSE(HuggingFaceProvider::is_reasoning_model("google/gemma-2b"));
}
