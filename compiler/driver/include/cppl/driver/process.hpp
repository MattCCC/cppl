#pragma once

#include <string>
#include <vector>

namespace cppl::driver {

struct ProcessResult {
    bool started = false;
    int exit_code = -1;
    std::string error;
};

// Runs a program with the given arguments, inheriting standard streams so that
// Clang's diagnostics reach the user exactly as Clang wrote them.
[[nodiscard]] ProcessResult run(const std::string& executable,
                                const std::vector<std::string>& arguments);

}  // namespace cppl::driver
