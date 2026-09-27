// The trusted kernel over derivations decoded from bytes (docs/KERNEL.md,
// "Testing"; TRUST.md TCB-CORE-001, TCB-TEST-001).
//
// The bytes choose, one decision at a time, a derivation the way
// tests/support/kernel builds one: each rule's premises derived or supposed,
// and at some steps a deliberate defect. The kernel decides.
//
// Properties: the verdict is deterministic, an acceptance carries the goal,
// and an accepted goal is not false in any interpretation of the independent
// model. Rejection is never a finding; acceptance of a falsehood always is.

#include "cppl/testing/fuzz.hpp"
#include "cppl/testing/kernel_generator.hpp"
#include "cppl/testing/kernel_oracle.hpp"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace gen = cppl::testing::kernel_generator;
    gen::ByteChoices choices({data, size});
    const gen::Sample sample = gen::generate(choices, gen::Mode::Derivation);
    cppl::testing::kernel_oracle::Statistics statistics;
    cppl::testing::fuzz::require_none(cppl::testing::kernel_oracle::examine(sample, statistics));
    return 0;
}
