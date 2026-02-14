#include <catch2/catch_test_macros.hpp>

#include "openclaw/providers/synthetic.hpp"

using namespace openclaw::providers;
using json = nlohmann::json;

TEST_CASE("Synthetic catalog lookup", "[providers][synthetic]") {
    SECTION("Contains known models") {
        auto catalog = SyntheticProvider::static_catalog();
        REQUIRE(!catalog.empty());

        bool found_deepseek = false;
        bool found_qwen = false;
        bool found_kimi = false;
        for (const auto& model : catalog) {
            if (model.id.find("deepseek") != std::string::npos) found_deepseek = true;
            if (model.id.find("qwen") != std::string::npos) found_qwen = true;
            if (model.id.find("kimi") != std::string::npos) found_kimi = true;
        }
        CHECK(found_deepseek);
        CHECK(found_qwen);
        CHECK(found_kimi);
    }

    SECTION("Catalog has at least 20 models") {
        auto catalog = SyntheticProvider::static_catalog();
        CHECK(catalog.size() >= 20);
    }
}

TEST_CASE("Synthetic hf: prefix resolution", "[providers][synthetic]") {
    SECTION("Resolves hf: prefix to model ID") {
        auto resolved = SyntheticProvider::resolve_hf_model("hf:deepseek-ai/DeepSeek-R1");
        CHECK(!resolved.empty());
    }

    SECTION("Unknown hf: prefix returns input as-is") {
        auto resolved = SyntheticProvider::resolve_hf_model("hf:unknown/model-xyz");
        CHECK(resolved == "hf:unknown/model-xyz");
    }

    SECTION("Non-hf: prefix returns as-is") {
        auto resolved = SyntheticProvider::resolve_hf_model("some-model-id");
        CHECK(resolved == "some-model-id");
    }
}

TEST_CASE("Synthetic reasoning flags", "[providers][synthetic]") {
    CHECK(SyntheticProvider::is_reasoning_model("deepseek-r1"));
    CHECK(SyntheticProvider::is_reasoning_model("qwen3-think-32b"));
    CHECK(SyntheticProvider::is_reasoning_model("some-reason-model"));

    CHECK_FALSE(SyntheticProvider::is_reasoning_model("glm-4.5-flash"));
    CHECK_FALSE(SyntheticProvider::is_reasoning_model("llama-3.3-70b"));
}
