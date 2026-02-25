#include <catch2/catch_test_macros.hpp>

#include "openclaw/routing/turn_source.hpp"

using namespace openclaw::routing;

TEST_CASE("Turn source channel takes precedence over session", "[routing][turn_source]") {
    TurnSourceMetadata ts;
    ts.channel = "telegram";

    CHECK(resolve_origin_message_provider(ts, "discord") == "telegram");
}

TEST_CASE("Session channel used when turn source empty", "[routing][turn_source]") {
    TurnSourceMetadata ts;
    CHECK(resolve_origin_message_provider(ts, "discord") == "discord");
}

TEST_CASE("Turn source 'to' takes precedence", "[routing][turn_source]") {
    TurnSourceMetadata ts;
    ts.to = "12345";
    CHECK(resolve_origin_to(ts, "99999") == "12345");
}

TEST_CASE("Session 'to' used when turn source empty", "[routing][turn_source]") {
    TurnSourceMetadata ts;
    CHECK(resolve_origin_to(ts, "99999") == "99999");
}

TEST_CASE("Turn source account_id takes precedence", "[routing][turn_source]") {
    TurnSourceMetadata ts;
    ts.account_id = "acct_1";
    CHECK(resolve_origin_account_id(ts, "acct_2") == "acct_1");
}

TEST_CASE("Session account_id used when turn source empty", "[routing][turn_source]") {
    TurnSourceMetadata ts;
    CHECK(resolve_origin_account_id(ts, "acct_2") == "acct_2");
}

TEST_CASE("Empty turn source string treated as absent", "[routing][turn_source]") {
    TurnSourceMetadata ts;
    ts.channel = "";
    ts.to = "";
    ts.account_id = "";
    CHECK(resolve_origin_message_provider(ts, "slack") == "slack");
    CHECK(resolve_origin_to(ts, "target") == "target");
    CHECK(resolve_origin_account_id(ts, "acct") == "acct");
}
