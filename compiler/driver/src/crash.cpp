#include "cppl/driver/crash.hpp"

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace cppl::driver {

namespace {

// A handler runs where the process is already broken, so the report allocates
// nothing, formats nothing and reads only these two pointers.
std::atomic<const char*> current_stage{"starting up"};
std::atomic<const char*> current_input{nullptr};

void write_text(const char* text) noexcept {
    if (text == nullptr) {
        return;
    }
    std::size_t length = 0;
    while (text[length] != '\0')
        ++length;
#ifdef _WIN32
    const HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(handle, text, static_cast<DWORD>(length), &written, nullptr);
    }
#else
    const ssize_t written = ::write(STDERR_FILENO, text, length);
    static_cast<void>(written);
#endif
}

void write_hex(std::uint64_t value) noexcept {
    char text[19] = "0x";
    std::size_t length = 2;
    bool started = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const auto digit = static_cast<unsigned>((value >> shift) & 0xF);
        if (digit == 0 && !started && shift != 0) {
            continue;
        }
        started = true;
        text[length++] = "0123456789abcdef"[digit];
    }
    text[length] = '\0';
    write_text(text);
}

void report(const char* kind, std::uint64_t code, const void* address) noexcept {
    write_text("cppl: internal error: ");
    write_text(kind);
    write_text(" while ");
    write_text(current_stage.load(std::memory_order_relaxed));
    if (const char* input = current_input.load(std::memory_order_relaxed)) {
        write_text(" of '");
        write_text(input);
        write_text("'");
    }
    write_text(" (code ");
    write_hex(code);
    if (address != nullptr) {
        write_text(" at ");
        write_hex(std::bit_cast<std::uintptr_t>(address));
    }
    write_text(")\n");
}

#ifdef _WIN32

const char* describe(DWORD code) noexcept {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
            return "an invalid memory access";
        case EXCEPTION_STACK_OVERFLOW:
            return "a stack overflow";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "an integer division by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "an illegal instruction";
        case EXCEPTION_IN_PAGE_ERROR:
            return "an unreadable page";
        default:
            return "a fatal exception";
    }
}

LONG WINAPI on_exception(EXCEPTION_POINTERS* information) noexcept {
    const EXCEPTION_RECORD* record = information != nullptr ? information->ExceptionRecord : nullptr;
    const DWORD code = record != nullptr ? record->ExceptionCode : 0;
    report(describe(code), code, record != nullptr ? record->ExceptionAddress : nullptr);
    return EXCEPTION_EXECUTE_HANDLER;
}

#else

const char* describe(int number) noexcept {
    switch (number) {
        case SIGSEGV:
            return "an invalid memory access";
        case SIGBUS:
            return "a bus error";
        case SIGFPE:
            return "an arithmetic fault";
        case SIGILL:
            return "an illegal instruction";
        case SIGABRT:
            return "an abort";
        default:
            return "a fatal signal";
    }
}

// The handler's own stack.
//
// The signal this most often reports is a stack overflow, and a handler that
// ran on the overflowed stack would fault again the moment it needed a frame.
// `SA_RESETHAND` does not save it: the second fault is delivered on the same
// exhausted stack, before the default action can take effect, and the process
// spins in the handler instead of dying. Giving the handler its own stack is
// what makes a stack overflow reportable at all.
alignas(std::max_align_t) char handler_stack[SIGSTKSZ < 65536 ? 65536 : SIGSTKSZ];

extern "C" void on_signal(int number, siginfo_t* information, void*) {
    // A second fault inside the report would re-enter this handler. The report
    // is best-effort and the process is already lost, so the re-entry is
    // dropped rather than retried: the fall-through below still ends the
    // process with the right signal.
    static std::atomic_flag reporting = ATOMIC_FLAG_INIT;
    if (!reporting.test_and_set()) {
        report(describe(number), static_cast<std::uint64_t>(number),
               information != nullptr ? information->si_addr : nullptr);
    }
    // The handler was reset when it ran, so this ends the process the way the
    // operating system meant to, and a caller still sees the signal.
    static_cast<void>(::raise(number));
    // `raise` returns if the signal is blocked while this handler runs, which
    // leaves a fault that cannot be resumed looping forever. Nothing about the
    // process is trustworthy here, so leave without running any more code.
    ::_exit(128 + number);
}

#endif

} // namespace

void install_crash_report() {
#ifdef _WIN32
    SetUnhandledExceptionFilter(on_exception);
#else
    // Install the handler's own stack first: SA_ONSTACK is what lets a stack
    // overflow be reported instead of faulting the handler too.
    stack_t stack{};
    stack.ss_sp = handler_stack;
    stack.ss_size = sizeof(handler_stack);
    stack.ss_flags = 0;
    static_cast<void>(::sigaltstack(&stack, nullptr));

    struct sigaction action{};
    action.sa_sigaction = on_signal;
    // glibc types the flag macros as `unsigned` but `sa_flags` as `int`, so
    // the combination has to be narrowed explicitly. The value is a small
    // constant bitmask, so the conversion discards nothing.
    action.sa_flags = static_cast<int>(SA_SIGINFO | SA_RESETHAND | SA_ONSTACK);
    sigemptyset(&action.sa_mask);
    for (const int number : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
        ::sigaction(number, &action, nullptr);
#endif
}

Stage::Stage(const char* name, const char* input) noexcept
    : previous_name_(current_stage.load(std::memory_order_relaxed)),
      previous_input_(current_input.load(std::memory_order_relaxed)) {
    current_stage.store(name, std::memory_order_relaxed);
    if (input != nullptr) {
        current_input.store(input, std::memory_order_relaxed);
    }
}

Stage::~Stage() noexcept {
    current_stage.store(previous_name_, std::memory_order_relaxed);
    current_input.store(previous_input_, std::memory_order_relaxed);
}

} // namespace cppl::driver
