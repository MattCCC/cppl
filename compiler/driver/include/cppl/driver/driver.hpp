#pragma once

namespace cppl::driver {

// The compiler driver.
//
// It orchestrates the stages and decides the process exit status. It is not a
// proof authority: it reports what the kernel decided and refuses to produce a
// program when a required obligation was not discharged (ARCHITECTURE.md 5).
[[nodiscard]] int run_driver(int argc, const char* const* argv);

} // namespace cppl::driver
