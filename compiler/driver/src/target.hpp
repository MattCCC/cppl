#pragma once

// The target a unit's program is compiled for, as the Clang driver that
// compiles it selects it, so that the analysis the verifier reads is made for
// that target and for no other (TRUST.md TCB-CLANG-006, SPEC.md ARITH-014).
//
// The driver and the libclang the bridge parses with are separate programs. The
// driver may select a target its arguments do not name: from the target prefix
// of its own name (`i686-linux-gnu-clang++`) or from a configuration file it
// reads. libclang sees only the arguments, so left to itself it would resolve
// the unit for the host while the object is compiled for the driver's target,
// and a contract would be proven of integer widths the program does not have.
//
// This header is a private implementation detail of cppl_driver.

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cppl::driver::detail {

// What the Clang driver writes to its standard output for `arguments`, read
// through `answer`, or nothing when it could not be run, did not succeed, or
// wrote more than an answer to a question can be.
[[nodiscard]] std::optional<std::string> ask_driver(const std::string& clang, const std::vector<std::string>& arguments,
                                                    const std::filesystem::path& answer);

struct CompileTarget {
    // The target triple the driver selects for the arguments
    // (`-print-target-triple`). The analysis is given it explicitly.
    std::string triple;
    // The triple the driver's compile job runs with (`-print-effective-triple`):
    // what the program's code is generated for, and so what the analysis must
    // have been made for.
    std::string effective;
    // Every configuration file the driver reads for the arguments, in order.
    // The analysis reads exactly these and no default of its own.
    std::vector<std::string> configuration_files;
};

// Asks `clang` what it compiles for under `arguments`. Fails, saying why, when
// it cannot be asked or an answer is missing or malformed. Arguments that
// compile for several targets at once (more than one `-arch`) never reach it:
// the driver refuses to preprocess under them, and a unit is read only after it
// is preprocessed.
[[nodiscard]] std::expected<CompileTarget, std::string> compile_target(const std::string& clang,
                                                                       const std::vector<std::string>& arguments);

// `arguments` as the analysis is to be given them: without the configuration
// files they name, and followed by an instruction to read no configuration
// file by default, the configuration files the driver reads, as it found them,
// and the driver's target triple.
[[nodiscard]] std::vector<std::string> analysis_arguments(const std::vector<std::string>& arguments,
                                                          const CompileTarget& target);

} // namespace cppl::driver::detail
