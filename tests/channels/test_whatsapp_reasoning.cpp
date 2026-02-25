#include <catch2/catch_test_macros.hpp>

#include "openclaw/channels/whatsapp.hpp"

using namespace openclaw::channels;

TEST_CASE("suppress_reasoning_payload strips Reasoning: prefix", "[channels][whatsapp][reasoning]") {
    auto result = WhatsAppChannel::suppress_reasoning_payload(
        "Reasoning: I need to think about this.\n\nHere is the answer.");
    CHECK(result == "Here is the answer.");
}

TEST_CASE("suppress_reasoning_payload returns full text without Reasoning:", "[channels][whatsapp][reasoning]") {
    auto result = WhatsAppChannel::suppress_reasoning_payload("Here is a normal response.");
    CHECK(result == "Here is a normal response.");
}

TEST_CASE("suppress_reasoning_payload handles empty string", "[channels][whatsapp][reasoning]") {
    auto result = WhatsAppChannel::suppress_reasoning_payload("");
    CHECK(result.empty());
}

TEST_CASE("suppress_reasoning_payload strips entire reasoning-only text", "[channels][whatsapp][reasoning]") {
    auto result = WhatsAppChannel::suppress_reasoning_payload("Reasoning: This is just reasoning.");
    CHECK(result.empty());
}
