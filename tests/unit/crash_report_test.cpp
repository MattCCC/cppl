// The crash report for a stack overflow (compiler/driver/src/crash.cpp). The
// handler runs on a stack of its own, so it can write its report and let the
// process die by the signal, instead of faulting again on the exhausted stack
// and spinning inside the handler.

#include "cppl/testing/test.hpp"

#if !defined(_WIN32)

#include "cppl/driver/crash.hpp"

#include <chrono>
#include <csignal>
#include <cstddef>
#include <string>
#include <thread>

// waitpid, fork, pipe, dup2 and the wait status macros are POSIX. glibc
// defines the macros in whichever of <stdlib.h> and <sys/wait.h> is read first,
// and a standard header has always read <stdlib.h> by then; macOS defines them
// in <sys/wait.h> alone. Both are included so each platform finds its own.
#include <signal.h>
#include <stdlib.h> // IWYU pragma: keep
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

volatile bool keep_recursing = true;

// Each frame keeps a page of its own, so the stack runs out quickly and the
// calls cannot be folded into a loop.
[[gnu::noinline]] std::size_t overflow(std::size_t depth) {
    volatile char frame[4096];
    frame[0] = static_cast<char>(depth);
    if (!keep_recursing) {
        return static_cast<std::size_t>(frame[0]);
    }
    return overflow(depth + 1) + static_cast<std::size_t>(frame[0]);
}

} // namespace

CPPL_TEST(a_stack_overflow_is_reported_and_ends_the_process_by_its_signal) {
    int ends[2] = {-1, -1};
    CPPL_CHECK_EQ(::pipe(ends), 0);
    const pid_t child = ::fork();
    CPPL_CHECK(child >= 0);
    if (child == 0) {
        static_cast<void>(::dup2(ends[1], STDERR_FILENO));
        static_cast<void>(::close(ends[0]));
        const cppl::driver::Stage stage("overflowing the stack on purpose");
        cppl::driver::install_crash_report();
        static_cast<void>(overflow(0));
        ::_exit(0);
    }
    static_cast<void>(::close(ends[1]));

    // A handler with no stack of its own never returns, so the child gets a
    // deadline rather than an unbounded wait.
    int status = 0;
    pid_t ended = 0;
    for (int tick = 0; tick < 600 && ended == 0; ++tick) {
        ended = ::waitpid(child, &status, WNOHANG);
        if (ended == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    if (ended == 0) {
        static_cast<void>(::kill(child, SIGKILL));
        static_cast<void>(::waitpid(child, &status, 0));
    }

    std::string report;
    char buffer[512];
    for (ssize_t read = ::read(ends[0], buffer, sizeof buffer); read > 0;
         read = ::read(ends[0], buffer, sizeof buffer)) {
        report.append(buffer, static_cast<std::size_t>(read));
    }
    static_cast<void>(::close(ends[0]));

    CPPL_CHECK(ended == child);
    // Killed by the fault itself, as a caller waiting on the process sees a
    // crash, not an exit status chosen to resemble one.
    CPPL_CHECK(WIFSIGNALED(status));
    if (WIFSIGNALED(status)) {
        const int signal = WTERMSIG(status);
        const bool fault = signal == SIGSEGV || signal == SIGBUS;
        CPPL_CHECK(fault);
    }
    CPPL_CHECK(report.starts_with("cppl: internal error: "));
    CPPL_CHECK(report.find(" while overflowing the stack on purpose (code ") != std::string::npos);
}

#else

CPPL_TEST(a_stack_overflow_is_reported_through_structured_exceptions_on_windows) {
    // Windows reports a stack overflow as an exception with a guard page of
    // its own; there is no alternate signal stack to check here.
}

#endif
