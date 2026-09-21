#pragma once

#include <filesystem>
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
[[nodiscard]] ProcessResult run(const std::string& executable, const std::vector<std::string>& arguments);

// Runs a program with the given arguments, redirecting its standard output to
// `output_path` (truncated first) instead of inheriting the caller's. Standard
// error still reaches the caller directly. Used for tools whose result is
// their stdout stream itself (e.g. `clang-format --output-replacements-xml`),
// where inheriting stdout as `run()` does would mix that result into whatever
// else the caller's own stdout carries (for `cppl-lsp`, the JSON-RPC channel).
[[nodiscard]] ProcessResult run_capturing_stdout(const std::string& executable,
                                                 const std::vector<std::string>& arguments,
                                                 const std::filesystem::path& output_path);

} // namespace cppl::driver
