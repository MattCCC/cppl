#include "cppl/testing/parallel.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace cppl::testing {

namespace {

// CPPL_TEST_JOBS, read as tests/support/parallel.sh reads it: unset or empty
// is one thread per processor, and anything but a positive count is one.
std::size_t jobs() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read before any worker thread starts.
    const char* named = std::getenv("CPPL_TEST_JOBS");
    if (named == nullptr || *named == '\0') {
        const unsigned processors = std::thread::hardware_concurrency();
        return processors == 0 ? 1 : processors;
    }
    const std::string_view text{named};
    std::size_t count = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), count);
    if (error != std::errc{} || end != text.data() + text.size() || count == 0) {
        return 1;
    }
    return count;
}

} // namespace

void for_each_index(std::size_t count, const std::function<void(std::size_t)>& body) {
    const std::size_t threads = std::min(jobs(), count);
    if (threads <= 1) {
        for (std::size_t index = 0; index < count; ++index) {
            body(index);
        }
        return;
    }

    // Indices are handed out in increasing order. A worker stops taking them
    // once one past the earliest known failure comes up, so every index before
    // the earliest failure is still run: whichever worker takes it, it is
    // below every failure known at the time.
    constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> earliest{kNone};
    std::mutex guard;
    std::exception_ptr failure;

    const auto work = [&] {
        for (;;) {
            const std::size_t index = next.fetch_add(1);
            if (index >= count || index > earliest.load()) {
                return;
            }
            try {
                body(index);
            } catch (...) {
                const std::scoped_lock lock{guard};
                if (index < earliest.load()) {
                    earliest.store(index);
                    failure = std::current_exception();
                }
                return;
            }
        }
    };

    // A thread the system refuses to start leaves the indices to the workers
    // that did start and to this thread, which takes them until none are left.
    std::vector<std::thread> workers;
    workers.reserve(threads - 1);
    for (std::size_t worker = 1; worker < threads; ++worker) {
        try {
            workers.emplace_back(work);
        } catch (const std::system_error&) {
            break;
        }
    }
    work();
    for (std::thread& worker : workers) {
        worker.join();
    }

    if (failure) {
        std::rethrow_exception(failure);
    }
}

} // namespace cppl::testing
