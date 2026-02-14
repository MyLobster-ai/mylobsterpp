#include <catch2/catch_test_macros.hpp>

#include "openclaw/infra/delivery_queue.hpp"

#include <filesystem>

using namespace openclaw::infra;
using json = nlohmann::json;

TEST_CASE("DeliveryQueue enqueue/ack/fail lifecycle", "[infra][delivery_queue]") {
    auto tmp_dir = std::filesystem::temp_directory_path() / "test_delivery_queue";
    std::filesystem::remove_all(tmp_dir);

    DeliveryQueue queue(tmp_dir);

    SECTION("Enqueue creates a file") {
        QueuedDelivery delivery;
        delivery.channel = "test";
        delivery.to = "user123";

        DeliveryPayload payload;
        payload.text = "Hello, world!";
        delivery.payloads.push_back(std::move(payload));

        auto id_result = queue.enqueue(std::move(delivery));
        REQUIRE(id_result.has_value());
        CHECK(!id_result->empty());

        // File should exist
        auto path = tmp_dir / (*id_result + ".json");
        CHECK(std::filesystem::exists(path));
    }

    SECTION("Ack removes the file") {
        QueuedDelivery delivery;
        delivery.channel = "test";
        delivery.to = "user123";
        DeliveryPayload payload;
        payload.text = "test";
        delivery.payloads.push_back(std::move(payload));

        auto id = *queue.enqueue(std::move(delivery));

        auto ack_result = queue.ack(id);
        CHECK(ack_result.has_value());

        auto path = tmp_dir / (id + ".json");
        CHECK_FALSE(std::filesystem::exists(path));
    }

    SECTION("Fail increments retry count") {
        QueuedDelivery delivery;
        delivery.channel = "test";
        delivery.to = "user123";
        DeliveryPayload payload;
        payload.text = "test";
        delivery.payloads.push_back(std::move(payload));

        auto id = *queue.enqueue(std::move(delivery));

        auto fail_result = queue.fail(id, "network error");
        CHECK(fail_result.has_value());

        // Load and verify retry count
        auto pending = queue.load_pending();
        REQUIRE(pending.size() == 1);
        CHECK(pending[0].retry_count == 1);
        CHECK(pending[0].last_error == "network error");
    }

    SECTION("Ack on non-existent returns NotFound") {
        auto result = queue.ack("nonexistent");
        REQUIRE(!result.has_value());
        CHECK(result.error().code() == openclaw::ErrorCode::NotFound);
    }

    // Cleanup
    std::filesystem::remove_all(tmp_dir);
}

TEST_CASE("DeliveryQueue backoff calculation", "[infra][delivery_queue]") {
    CHECK(DeliveryQueue::backoff_delay(0) == std::chrono::seconds{0});
    CHECK(DeliveryQueue::backoff_delay(1) == std::chrono::seconds{5});
    CHECK(DeliveryQueue::backoff_delay(2) == std::chrono::seconds{25});
    CHECK(DeliveryQueue::backoff_delay(3) == std::chrono::seconds{120});
    CHECK(DeliveryQueue::backoff_delay(4) == std::chrono::seconds{600});
    // Beyond max uses last value
    CHECK(DeliveryQueue::backoff_delay(5) == std::chrono::seconds{600});
}

TEST_CASE("DeliveryQueue load_pending sorts oldest first", "[infra][delivery_queue]") {
    auto tmp_dir = std::filesystem::temp_directory_path() / "test_delivery_queue_sort";
    std::filesystem::remove_all(tmp_dir);

    DeliveryQueue queue(tmp_dir);

    // Enqueue two deliveries with different timestamps
    QueuedDelivery d1;
    d1.channel = "test";
    d1.to = "user1";
    d1.enqueued_at = Clock::now() - std::chrono::seconds(10);
    DeliveryPayload p1;
    p1.text = "first";
    d1.payloads.push_back(std::move(p1));

    QueuedDelivery d2;
    d2.channel = "test";
    d2.to = "user2";
    d2.enqueued_at = Clock::now();
    DeliveryPayload p2;
    p2.text = "second";
    d2.payloads.push_back(std::move(p2));

    queue.enqueue(std::move(d2));  // Enqueue newer first
    queue.enqueue(std::move(d1));  // Enqueue older second

    auto pending = queue.load_pending();
    REQUIRE(pending.size() == 2);
    // Should be sorted oldest first
    CHECK(pending[0].to == "user1");
    CHECK(pending[1].to == "user2");

    std::filesystem::remove_all(tmp_dir);
}
