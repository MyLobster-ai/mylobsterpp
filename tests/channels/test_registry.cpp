#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string_view>

#include "openclaw/channels/registry.hpp"

using namespace openclaw::channels;

/// Minimal stub channel for testing the registry.
class StubChannel : public Channel {
public:
    explicit StubChannel(std::string name, std::string type_id = "stub")
        : name_(std::move(name)), type_(std::move(type_id)) {}

    auto start() -> boost::asio::awaitable<void> override { running_ = true; co_return; }
    auto stop() -> boost::asio::awaitable<void> override { running_ = false; co_return; }
    auto send(OutgoingMessage) -> boost::asio::awaitable<openclaw::Result<void>> override {
        co_return ok_result();
    }
    [[nodiscard]] auto name() const -> std::string_view override { return name_; }
    [[nodiscard]] auto type() const -> std::string_view override { return type_; }
    [[nodiscard]] auto is_running() const noexcept -> bool override { return running_; }

private:
    std::string name_;
    std::string type_;
    bool running_ = false;
};

TEST_CASE("ChannelRegistry starts empty", "[channels][registry]") {
    ChannelRegistry reg;

    CHECK(reg.empty());
    CHECK(reg.size() == 0);
    CHECK(reg.list().empty());
}

TEST_CASE("ChannelRegistry register and lookup", "[channels][registry]") {
    ChannelRegistry reg;

    reg.register_channel(std::make_unique<StubChannel>("my-telegram", "telegram"));
    reg.register_channel(std::make_unique<StubChannel>("my-discord", "discord"));

    SECTION("size and empty reflect registrations") {
        CHECK(reg.size() == 2);
        CHECK_FALSE(reg.empty());
    }

    SECTION("get returns registered channel") {
        auto* ch = reg.get("my-telegram");
        REQUIRE(ch != nullptr);
        CHECK(ch->type() == "telegram");
    }

    SECTION("get returns nullptr for unknown name") {
        auto* ch = reg.get("nonexistent");
        CHECK(ch == nullptr);
    }

    SECTION("list returns all channel names") {
        auto names = reg.list();
        REQUIRE(names.size() == 2);
        // Check both names are present (order may vary)
        bool has_telegram = false;
        bool has_discord = false;
        for (auto n : names) {
            if (n == "my-telegram") has_telegram = true;
            if (n == "my-discord") has_discord = true;
        }
        CHECK(has_telegram);
        CHECK(has_discord);
    }
}

TEST_CASE("ChannelRegistry register replaces existing", "[channels][registry]") {
    ChannelRegistry reg;

    reg.register_channel(std::make_unique<StubChannel>("bot", "telegram"));
    reg.register_channel(std::make_unique<StubChannel>("bot", "discord"));

    CHECK(reg.size() == 1);
    auto* ch = reg.get("bot");
    REQUIRE(ch != nullptr);
    CHECK(ch->type() == "discord");  // Replaced
}

TEST_CASE("ChannelRegistry unregister", "[channels][registry]") {
    ChannelRegistry reg;

    reg.register_channel(std::make_unique<StubChannel>("removable", "test"));
    REQUIRE(reg.size() == 1);

    SECTION("unregister existing returns the channel") {
        auto removed = reg.unregister_channel("removable");
        REQUIRE(removed != nullptr);
        CHECK(removed->name() == "removable");
        CHECK(reg.empty());
    }

    SECTION("unregister nonexistent returns nullptr") {
        auto removed = reg.unregister_channel("nope");
        CHECK(removed == nullptr);
        CHECK(reg.size() == 1);
    }
}

TEST_CASE("ChannelRegistry const get", "[channels][registry]") {
    ChannelRegistry reg;
    reg.register_channel(std::make_unique<StubChannel>("ch1", "type1"));

    const auto& const_reg = reg;
    const auto* ch = const_reg.get("ch1");
    REQUIRE(ch != nullptr);
    CHECK(ch->name() == "ch1");

    const auto* missing = const_reg.get("missing");
    CHECK(missing == nullptr);
}
