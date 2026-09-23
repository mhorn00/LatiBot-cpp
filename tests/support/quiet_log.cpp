#include "core/util/log.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {

/// Silences the bot's logger for the whole test run.
///
/// A debug build now defaults to the debug level (see `util::default_log_level`),
/// which would otherwise put every migration, stage decision and trigger match
/// on stderr in the middle of the test output. Tests that care about logging
/// use `testing::capture_log`, which sets the level it needs and restores this
/// one afterwards.
class quiet_log final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override { latibot::util::log().set_level(latibot::util::log_level::off); }
};

CATCH_REGISTER_LISTENER(quiet_log)

} // namespace
