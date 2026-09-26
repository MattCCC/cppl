#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::driver {

struct Input {
    std::string path;
    std::size_t argument_index = 0; // position in Options::arguments
    bool is_header = false;
};

// The command line, split into what C++L consumes and what Clang receives.
//
// Everything C++L does not own is preserved in order and handed to Clang
// unchanged: include paths, defines, warnings, target flags, optimization,
// linker arguments (ARCHITECTURE.md 80).
struct Options {
    std::vector<std::string> arguments;
    std::vector<Input> inputs;
    std::string clang;
    bool trust_report = false;
    // Where to write the runtime program for inspection. Observability only:
    // it changes nothing about what is compiled.
    std::string emit_projection;
    // Where to write this unit's verification interface: the contracts it
    // proved, for other units to use (SPEC.md TUBOUND-002). Empty means none.
    std::string emit_interface;
    // Verification interfaces of other units whose contracts this compile may
    // use, each validated before any of it is believed (SPEC.md TUBOUND-003).
    std::vector<std::string> import_interfaces;
    bool passthrough = false;       // the command does not compile anything
    bool explicit_language = false; // -x was given
    std::string standard;           // -std=..., for reporting
    std::vector<std::string> errors;
};

[[nodiscard]] Options parse(int argc, const char* const* argv);

[[nodiscard]] bool is_source_path(std::string_view path);
[[nodiscard]] bool is_header_path(std::string_view path);

} // namespace cppl::driver
