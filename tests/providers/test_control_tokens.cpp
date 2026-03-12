#include <catch2/catch_test_macros.hpp>
#include "openclaw/providers/provider.hpp"

using namespace openclaw::providers;

TEST_CASE("strip_control_tokens removes standard delimiters", "[providers]") {
    CHECK(strip_control_tokens("Hello <|endoftext|> world") == "Hello  world");
    CHECK(strip_control_tokens("<|im_start|>user<|im_end|>") == "user");
    CHECK(strip_control_tokens("No tokens here") == "No tokens here");
    CHECK(strip_control_tokens("") == "");
    CHECK(strip_control_tokens("before<|token|>after") == "beforeafter");
}

TEST_CASE("truncate_tool_result head+tail strategy", "[providers]") {
    std::string text(1000, 'x');
    auto result = truncate_tool_result(text, 200);
    CHECK(result.size() < 300);  // Result should be bounded
    CHECK(result.starts_with("xxxx"));
    CHECK(result.ends_with("xxxx"));
    CHECK(result.find("[truncated") != std::string::npos);

    // Short text passes through unchanged
    CHECK(truncate_tool_result("short", 200) == "short");
}

TEST_CASE("classify_billing_error identifies Venice/Poe 402", "[providers]") {
    CHECK(classify_billing_error(402, "venice") == FailoverCategory::RateLimit);
    CHECK(classify_billing_error(402, "poe") == FailoverCategory::RateLimit);
    CHECK(classify_billing_error(402, "anthropic") == FailoverCategory::None);
    CHECK(classify_billing_error(499, "anthropic") == FailoverCategory::Timeout);
    CHECK(classify_billing_error(200, "venice") == FailoverCategory::None);
}

TEST_CASE("decode_html_entities", "[providers]") {
    CHECK(decode_html_entities("a &amp; b") == "a & b");
    CHECK(decode_html_entities("&lt;div&gt;") == "<div>");
    CHECK(decode_html_entities("&quot;hello&quot;") == "\"hello\"");
    CHECK(decode_html_entities("plain text") == "plain text");
    CHECK(decode_html_entities("") == "");
}

TEST_CASE("is_gemini_retryable_error", "[providers]") {
    CHECK(is_gemini_retryable_error("MALFORMED_RESPONSE from model"));
    CHECK_FALSE(is_gemini_retryable_error("normal error"));
}

TEST_CASE("is_bedrock_quota_error", "[providers]") {
    CHECK(is_bedrock_quota_error("ThrottlingException: too many requests"));
    CHECK(is_bedrock_quota_error("ServiceQuotaExceededException"));
    CHECK_FALSE(is_bedrock_quota_error("normal error"));
}
