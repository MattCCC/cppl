#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::driver {

struct Input {
    std::string path;
    std::size_t argument_index = 0;  // position in Options::arguments
    bool is_header = false;
};

// The command line, split into what C++L consumes and what Clang receives.
//
// Everything C++L does not own is preserved in order and handed to Clang
// unchanged: include paths, defines, warnings, target flags, optimization,
// linker arguments (ARCHITECTURE.md 50).
struct Options {
    std::vector<std::string> arguments;
    std::vector<Input> inputs;
    std::string clang;
    bool trust_report = false;
    bool passthrough = false;         // the command does not compile anything
    bool explicit_language = false;   // -x was given
    std::string standard;             // -std=..., for reporting
    std::vector<std::string> errors;
};

[[nodiscard]] Options parse(int argc, const char* const* argv);

[[nodiscard]] bool is_source_path(std::string_view path);
[[nodiscard]] bool is_header_path(std::string_view path);

}  // namespace cppl::driver
