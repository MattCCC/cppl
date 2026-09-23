#include "cppl/driver/process.hpp"

#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <array>
#include <cstddef>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>

extern char** environ;
#endif

namespace cppl::driver {

#ifdef _WIN32

namespace {

std::string describe(DWORD error) {
    std::array<char, 512> text{};
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
                                        text.data(), static_cast<DWORD>(text.size()), nullptr);
    if (length == 0) {
        return "error " + std::to_string(error);
    }
    std::string message(text.data(), length);
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' ')) {
        message.pop_back();
    }
    return message;
}

// Windows hands the child a single command line and the child splits it again,
// so the quoting the C runtime and CommandLineToArgvW undo has to be applied
// here exactly. Without it a path holding a space arrives as two arguments.
void append(std::string& command, const std::string& argument) {
    if (!command.empty()) {
        command.push_back(' ');
    }
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string::npos) {
        command += argument;
        return;
    }

    command.push_back('"');
    for (std::size_t index = 0; index < argument.size();) {
        std::size_t backslashes = 0;
        while (index < argument.size() && argument[index] == '\\') {
            ++backslashes;
            ++index;
        }
        if (index == argument.size()) {
            // The run ends the argument, so it precedes the closing quote and
            // must not escape it.
            command.append(backslashes * 2, '\\');
            break;
        }
        if (argument[index] == '"') {
            command.append(backslashes * 2 + 1, '\\');
        } else {
            command.append(backslashes, '\\');
        }
        command.push_back(argument[index]);
        ++index;
    }
    command.push_back('"');
}

bool usable(HANDLE handle) {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

} // namespace

namespace {

ProcessResult spawn_and_wait(const std::string& executable, const std::vector<std::string>& arguments,
                             HANDLE stdout_handle) {
    std::string command;
    append(command, executable);
    for (const std::string& argument : arguments) {
        append(command, argument);
    }

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE output = usable(stdout_handle) ? stdout_handle : GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (usable(input) && usable(output) && usable(error)) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input;
        startup.hStdOutput = output;
        startup.hStdError = error;
    }

    // The executable is named only in the command line, where it is quoted, so
    // the search for it is unambiguous and '.exe' is supplied by Windows.
    PROCESS_INFORMATION process{};
    const BOOL started =
        CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process);
    if (started == 0) {
        return ProcessResult{false, -1, "could not run '" + executable + "': " + describe(GetLastError())};
    }
    CloseHandle(process.hThread);

    if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0) {
        const std::string reason = describe(GetLastError());
        CloseHandle(process.hProcess);
        return ProcessResult{true, -1, "could not wait for '" + executable + "': " + reason};
    }

    DWORD status = 0;
    const BOOL reported = GetExitCodeProcess(process.hProcess, &status);
    if (reported == 0) {
        const std::string reason = describe(GetLastError());
        CloseHandle(process.hProcess);
        return ProcessResult{true, -1, "could not read the exit status of '" + executable + "': " + reason};
    }
    CloseHandle(process.hProcess);

    // A process killed by an exception reports the exception code, which is
    // outside the range of an exit status; it stays distinct from success.
    return ProcessResult{true, static_cast<int>(status), {}};
}

} // namespace

ProcessResult run(const std::string& executable, const std::vector<std::string>& arguments) {
    return spawn_and_wait(executable, arguments, nullptr);
}

ProcessResult run_capturing_stdout(const std::string& executable, const std::vector<std::string>& arguments,
                                   const std::filesystem::path& output_path) {
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;

    const HANDLE file = CreateFileA(output_path.string().c_str(), GENERIC_WRITE, 0, &inheritable, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!usable(file)) {
        return ProcessResult{false, -1, "could not create '" + output_path.string() + "': " + describe(GetLastError())};
    }
    const ProcessResult result = spawn_and_wait(executable, arguments, file);
    CloseHandle(file);
    return result;
}

#else

namespace {

std::vector<char*> build_argv(const std::string& executable, const std::vector<std::string>& arguments,
                              std::vector<std::string>& words) {
    words.reserve(arguments.size() + 1);
    words.push_back(executable);
    words.insert(words.end(), arguments.begin(), arguments.end());

    std::vector<char*> argv;
    argv.reserve(words.size() + 1);
    for (std::string& word : words) {
        argv.push_back(word.data());
    }
    argv.push_back(nullptr);
    return argv;
}

ProcessResult wait_for(pid_t child, const std::string& executable) {
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return ProcessResult{true, -1,
                                 "could not wait for '" + executable + "': " + std::generic_category().message(errno)};
        }
    }
    if (WIFEXITED(status)) {
        return ProcessResult{true, WEXITSTATUS(status), {}};
    }
    if (WIFSIGNALED(status)) {
        return ProcessResult{true, 128 + WTERMSIG(status),
                             "'" + executable + "' terminated by signal " + std::to_string(WTERMSIG(status)), true};
    }
    return ProcessResult{true, -1, "'" + executable + "' ended abnormally"};
}

} // namespace

ProcessResult run(const std::string& executable, const std::vector<std::string>& arguments) {
    std::vector<std::string> words;
    const std::vector<char*> argv = build_argv(executable, arguments, words);

    pid_t child = 0;
    const int spawned = posix_spawnp(&child, executable.c_str(), nullptr, nullptr, argv.data(), environ);
    if (spawned != 0) {
        return ProcessResult{false, -1,
                             "could not run '" + executable + "': " + std::generic_category().message(spawned)};
    }
    return wait_for(child, executable);
}

ProcessResult run_capturing_stdout(const std::string& executable, const std::vector<std::string>& arguments,
                                   const std::filesystem::path& output_path) {
    std::vector<std::string> words;
    const std::vector<char*> argv = build_argv(executable, arguments, words);

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return ProcessResult{false, -1, "could not prepare to run '" + executable + "'"};
    }
    // O_TRUNC|O_CREAT, 0600: only this process reads it back, from a private
    // scratch directory (ScratchDirectory), so it is created owner-only.
    const int opened = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, output_path.c_str(),
                                                        O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (opened != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return ProcessResult{false, -1, "could not redirect output for '" + executable + "'"};
    }

    pid_t child = 0;
    const int spawned = posix_spawnp(&child, executable.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) {
        return ProcessResult{false, -1,
                             "could not run '" + executable + "': " + std::generic_category().message(spawned)};
    }
    return wait_for(child, executable);
}

#endif

} // namespace cppl::driver
