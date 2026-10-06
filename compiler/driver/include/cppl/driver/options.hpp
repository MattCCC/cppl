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
    // Whether each argument is a value of the option before it, as Clang's
    // driver reads the command line (`separate_values`): never an input and
    // never an option of its own, and never parted from its option.
    std::vector<bool> option_value;
    std::vector<Input> inputs;
    // The position in `arguments` of every input, whatever it is -- a source
    // C++L reads, another source, an object, a library -- in order: each
    // argument that is neither an option nor an option's value.
    std::vector<std::size_t> positional;
    std::string clang;
    bool trust_report = false;
    // Where to write the trust report as a JSON document, for tools rather
    // than people (TRUST.md 36, Annex C). Empty means none.
    std::string emit_trust_report;
    // Where to write the runtime program for inspection. Observability only:
    // it changes nothing about what is compiled.
    std::string emit_projection;
    // Where to write this unit's verification interface: the contracts it
    // proved, for other units to use (SPEC.md TUBOUND-002). Empty means none.
    std::string emit_interface;
    // Verification interfaces of other units whose contracts this compile may
    // use, each validated before any of it is believed (SPEC.md TUBOUND-003).
    std::vector<std::string> import_interfaces;
    bool version = false;           // print what verification results are bound to, and stop
    bool passthrough = false;       // the command does not compile anything
    bool explicit_language = false; // -x was given
    bool compile_only = false;      // -c, -S or -fsyntax-only: nothing is linked
    std::string output;             // the last -o's value, or empty
    std::string standard;           // the selected standard (`selected_standard`)
    std::vector<std::string> errors;
};

[[nodiscard]] Options parse(int argc, const char* const* argv);

[[nodiscard]] bool is_source_path(std::string_view path);
[[nodiscard]] bool is_header_path(std::string_view path);

// How many of the arguments after `argument` Clang's driver reads as its
// values when `argument` stands where an option may: 1 for `-o` or
// `--include-directory`, 3 for `-sectcreate`, 0 for a flag, an option with its
// value joined (`-Ifoo`, `--config=x`) or an input. Taken from the option table
// of Clang 22, whose libclang makes the analysis and refuses an option it does
// not know. Every argument list C++L hands on is cut only between one option
// with its values and the next, so no option takes another's argument as its
// value in one command and not in another (SPEC.md ARITH-014).
[[nodiscard]] std::size_t separate_values(std::string_view argument);

// The C++ standard `arguments` select, as Clang reads them: the value of the
// last `-std=`, `--std=` or `--std`, the one Clang obeys, or empty when none
// does and the driver's default applies. An interface records it as the
// language mode it was verified in (SPEC.md TUBOUND-005).
[[nodiscard]] std::string selected_standard(const std::vector<std::string>& arguments);

} // namespace cppl::driver
