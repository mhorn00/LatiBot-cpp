#pragma once

#include <dpp/dpp.h>

#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <utility>

namespace latibot::testing {

/// The task's result, or nothing if it has not finished within `limit`.
///
/// What `dpp::task::sync_wait_for` is for, without its race: DPP's waiter
/// reads the result's variant unlocked while another thread writes it, and
/// can see the index halfway through, so a task finishing on another thread
/// just as the wait starts comes back as "timed out" at once. A future
/// waits properly. Use this for a task another thread finishes, such as the
/// speech engine's worker; a mock's task is done before the wait begins.
///
/// The task moves into a job that outlives a timeout, so nothing it touches
/// dangles if the test gives up first.
template <typename T>
[[nodiscard]] auto finished_within(dpp::task<T> task, std::chrono::milliseconds limit) -> std::optional<T> {
    auto done = std::make_shared<std::promise<T>>();
    std::future<T> result = done->get_future();
    [](dpp::task<T> awaited, std::shared_ptr<std::promise<T>> into) -> dpp::job {
        try {
            into->set_value(co_await std::move(awaited));
        } catch (...) {
            into->set_exception(std::current_exception());
        }
    }(std::move(task), done);
    if (result.wait_for(limit) != std::future_status::ready) return std::nullopt;
    return result.get();
}

} // namespace latibot::testing
