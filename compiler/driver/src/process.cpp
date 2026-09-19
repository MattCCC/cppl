#include "cppl/driver/process.hpp"

#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <vector>

extern char** environ;

namespace cppl::driver {

ProcessResult run(const std::string& executable, const std::vector<std::string>& arguments) {
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const std::string& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    pid_t child = 0;
    const int spawned = posix_spawnp(&child, executable.c_str(), nullptr, nullptr, argv.data(), environ);
    if (spawned != 0) {
        return ProcessResult{false, -1, "could not run '" + executable + "': " + std::strerror(spawned)};
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return ProcessResult{true, -1, "could not wait for '" + executable + "': " + std::strerror(errno)};
        }
    }

    if (WIFEXITED(status)) {
        return ProcessResult{true, WEXITSTATUS(status), {}};
    }
    if (WIFSIGNALED(status)) {
        return ProcessResult{true, 128 + WTERMSIG(status), "'" + executable + "' terminated by signal"};
    }
    return ProcessResult{true, -1, "'" + executable + "' ended abnormally"};
}

} // namespace cppl::driver
