#pragma once

// What `cppl --cppl-version` reports (SPEC.md TUBOUND-005; TRUST.md
// TCB-REPRO-001, TCB-VERSION-004).
//
// This header is a private implementation detail of cppl_driver.

#include <string>
#include <vector>

namespace cppl::driver::detail {

// Prints what this compiler's verification results are bound to and what a
// build of it is traced to: the compiler and the source it was built from, the
// verification semantics, the kernel, the interface format, the Clang that
// resolves C++ semantics and the Clang driver that compiles, and the target,
// language mode and standard library that driver selects for `arguments`.
//
// Every line is a function of the source, the toolchain and the arguments:
// nothing names a time, a user, a host or a path, so a clean checkout of one
// commit built against one toolchain prints the same record.
//
// Returns 0, or 1 when the Clang driver could not be asked what it selects, in
// which case the lines it would have answered say so.
[[nodiscard]] int print_version(const std::string& clang, const std::vector<std::string>& arguments,
                                const std::string& standard);

} // namespace cppl::driver::detail
