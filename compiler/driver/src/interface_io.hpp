#pragma once

// Reading and writing verification interfaces (SPEC.md TUBOUND-002 to TUBOUND-009,
// RFC 0017): the files, the configuration they are bound to, and the checks
// every imported one passes before any of its entries is offered to the
// obligation layer.
//
// This header is a private implementation detail of cppl_driver.

#include "cppl/artifact/interface.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/obligations/interface.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace cppl::driver::detail {

// What an imported interface must have been produced under (SPEC.md
// TUBOUND-005): this compiler's version and verification semantics, the kernel
// and formal-core versions, the Clang that resolves C++ semantics, the selected
// language mode, and the target triple the given Clang selects for `arguments`.
// The digest of this compiler's own build is left unset: an interface records
// it, and no reader compares it.
[[nodiscard]] std::expected<artifact::Configuration, std::string> compared_configuration(
    const std::string& clang, const std::vector<std::string>& arguments, const std::string& standard);

// What this compile would record as its configuration: the compared
// configuration and the digest of this compiler's own executable.
[[nodiscard]] std::expected<artifact::Configuration, std::string> current_configuration(
    const std::string& clang, const std::vector<std::string>& arguments, const std::string& standard);

// Reads each interface, refusing any that is malformed, of another format
// version, produced under another configuration, or stale because a file it
// was produced from has changed since, and then every entry another interface
// contradicts or whose dependencies are not recorded as they were proven. Each
// refusal is reported; what survives is what a unit may consult.
[[nodiscard]] obligations::Imports read_imports(const std::vector<std::string>& paths,
                                                const artifact::Configuration& configuration,
                                                diagnostics::Engine& engine);

// Every file Clang's preprocessor reads to preprocess `input` under
// `arguments`, as Clang itself lists them in a dependency rule: the input, each
// header, a file forced in with `-include` or `-imacros`, the header a
// precompiled header was built from, and each resource `#embed` pulls in,
// which no line marker names (SPEC.md TUBOUND-005, TRUST.md TCB-XTU-008). The
// arguments' own dependency options are left out, so no dependency file the
// build asked for is written by this. `header` reads the input as a header.
[[nodiscard]] std::expected<std::vector<std::string>, std::string> files_read(const std::string& clang,
                                                                              const std::vector<std::string>& arguments,
                                                                              const std::string& input, bool header,
                                                                              const std::filesystem::path& scratch);

// The files a unit was preprocessed from, each with its content now, for the
// interface to bind to (SPEC.md TUBOUND-005). `files` are the names the
// preprocessor's line markers gave and those `files_read` gave; a line-marker
// name that is not a file, such as `<built-in>`, is not one of them.
[[nodiscard]] std::expected<std::vector<artifact::SourceFile>, std::string> source_files(
    const std::vector<std::string>& files);

// Writes an interface so that no reader can observe a partial one: to a file
// beside the destination, renamed over it once complete.
[[nodiscard]] std::expected<void, std::string> write_interface(const std::string& path,
                                                               const artifact::Interface& recorded);

// Removes an interface left at `path` by an earlier compile, so a unit that no
// longer verifies does not leave one claiming that it does. A file at `path`
// that is not an interface is left alone.
void withdraw_interface(const std::string& path);

} // namespace cppl::driver::detail
