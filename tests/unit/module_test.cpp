// The module interface: capabilities, the order modules offer and start in,
// and what the host does with a listener (docs/modules/Module_Plan_Final.md
// §4 and §5).

#include "core/modules/module.hpp"
#include "core/modules/capability_registry.hpp"
#include "core/modules/host.hpp"

#include "support/capture_log.hpp"
#include "support/test_host.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using latibot::modules::capability_registry;
using latibot::modules::host;
using latibot::modules::module_list;
using latibot::testing::test_host;

namespace {

/// A capability, as `capabilities::speech` is one.
class greeting {
public:
    virtual ~greeting() = default;
    greeting() = default;
    greeting(const greeting&) = delete;
    auto operator=(const greeting&) -> greeting& = delete;

    [[nodiscard]] virtual auto greet() const -> std::string = 0;
};

class hello final : public greeting {
public:
    [[nodiscard]] auto greet() const -> std::string override { return "hello"; }
};

/// Another, which nobody offers.
class farewell {
public:
    virtual ~farewell() = default;
    farewell() = default;
    farewell(const farewell&) = delete;
    auto operator=(const farewell&) -> farewell& = delete;
};

/// Offers a greeting, and records when it was asked to.
class greeter final : public latibot::modules::module {
public:
    explicit greeter(std::vector<std::string>& journal) : journal_(&journal) {}

    [[nodiscard]] auto name() const -> std::string_view override { return "greeter"; }
    auto offer(capability_registry& offered) -> void override {
        journal_->emplace_back("greeter offers");
        offered.offer<greeting>(greeting_, name());
    }
    auto start(host& /*bot*/) -> void override { journal_->emplace_back("greeter starts"); }

private:
    std::vector<std::string>* journal_;
    hello greeting_;
};

/// Uses the greeting if somebody offers it.
class listener final : public latibot::modules::module {
public:
    explicit listener(std::vector<std::string>& journal) : journal_(&journal) {}

    [[nodiscard]] auto name() const -> std::string_view override { return "listener"; }
    auto start(host& bot) -> void override {
        const greeting* found = bot.capabilities().find<greeting>();
        journal_->push_back(found != nullptr ? "listener heard " + found->greet() : "listener heard nothing");
    }

private:
    std::vector<std::string>* journal_;
};

/// Refuses to start, as a module with a bad config section would.
class broken final : public latibot::modules::module {
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "broken"; }
    auto start(host& /*bot*/) -> void override { throw std::runtime_error("broken's config section is wrong"); }
};

} // namespace

TEST_CASE("a capability is found once offered, and null when nobody offers it", "[module]") {
    capability_registry offered;
    hello implementation;

    CHECK(offered.find<greeting>() == nullptr);
    CHECK(offered.offered_by<greeting>().empty());

    offered.offer<greeting>(implementation, "greeter");

    REQUIRE(offered.find<greeting>() != nullptr);
    CHECK(offered.find<greeting>()->greet() == "hello");
    CHECK(offered.offered_by<greeting>() == "greeter");
    CHECK(offered.find<farewell>() == nullptr);
    CHECK(offered.size() == 1);
}

TEST_CASE("one capability offered twice stops startup, naming both modules", "[module]") {
    // Which of the two a module would find is not something to leave to
    // the order the modules start in.
    capability_registry offered;
    hello first;
    hello second;
    offered.offer<greeting>(first, "dectalk");

    CHECK_THROWS_WITH(offered.offer<greeting>(second, "espeak"),
                      Catch::Matchers::ContainsSubstring("dectalk") && Catch::Matchers::ContainsSubstring("espeak"));
    CHECK(offered.find<greeting>() == &first);
}

TEST_CASE("every module offers before any starts, so the list's order does not matter", "[module]") {
    // The listener is first in the list and still finds the greeting the
    // module after it offers (docs/modules/Module_Plan_Final.md §4.3).
    std::vector<std::string> journal;
    test_host bot;

    const module_list modules = latibot::modules::start_modules(
        [&journal](host& /*host*/) {
            module_list made;
            made.push_back(std::make_unique<listener>(journal));
            made.push_back(std::make_unique<greeter>(journal));
            return made;
        },
        bot, bot.offered);

    CHECK(modules.size() == 2);
    CHECK(journal == std::vector<std::string>{"greeter offers", "listener heard hello", "greeter starts"});
}

TEST_CASE("without the module that offers it, a capability is null and the user copes", "[module]") {
    std::vector<std::string> journal;
    test_host bot;

    const module_list modules = latibot::modules::start_modules(
        [&journal](host& /*host*/) {
            module_list made;
            made.push_back(std::make_unique<listener>(journal));
            return made;
        },
        bot, bot.offered);

    CHECK(journal == std::vector<std::string>{"listener heard nothing"});
}

TEST_CASE("a module that throws while starting stops startup", "[module]") {
    test_host bot;

    CHECK_THROWS_WITH(latibot::modules::start_modules(
                          [](host& /*host*/) {
                              module_list made;
                              made.push_back(std::make_unique<broken>());
                              return made;
                          },
                          bot, bot.offered),
                      Catch::Matchers::ContainsSubstring("config section"));
}

TEST_CASE("a listener that throws is logged under its name, and DPP never sees it", "[module]") {
    const latibot::testing::capture_log captured(latibot::util::log_level::error);
    test_host bot;
    int calls = 0;

    bot.listen(bot.bot_cluster.on_message_delete, "midnight: deleted messages", [&calls](const dpp::message_delete_t& /*event*/) {
        ++calls;
        throw std::runtime_error("the store is gone");
    });

    CHECK(bot.listeners == std::vector<std::string>{"midnight: deleted messages"});
    CHECK_FALSE(bot.bot_cluster.on_message_delete.empty());

    CHECK_NOTHROW(bot.bot_cluster.on_message_delete.call(dpp::message_delete_t{}));
    CHECK(calls == 1);
    CHECK(captured.contains(latibot::util::log_level::error, "midnight: deleted messages failed: the store is gone"));
}

TEST_CASE("the test host fires a repeating timer each time, and a one-shot once", "[module]") {
    test_host bot;
    int ticks = 0;
    int once = 0;
    bot.every(std::chrono::seconds{30}, "a tick", [&ticks] { ++ticks; });
    bot.after(std::chrono::seconds{10}, "a delay", [&once] { ++once; });

    CHECK(bot.fire("a tick") == 1);
    CHECK(bot.fire("a tick") == 1);
    CHECK(bot.fire("a delay") == 1);
    CHECK(bot.fire("a delay") == 0);
    CHECK(ticks == 2);
    CHECK(once == 1);
}
