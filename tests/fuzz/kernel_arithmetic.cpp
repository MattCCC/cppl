// Linear arithmetic over machine integers, from bytes (docs/KERNEL.md,
// "Linear arithmetic"; SPEC.md 7.5; TRUST.md TCB-CORE-006, TCB-CORE-009).
//
// The first byte picks the source of evidence: the untrusted refutation search
// over facts and a goal the rest of the bytes choose (with, sometimes, a
// corrupted or invented certificate), or the untrusted automation over a goal
// they choose. Either way the kernel states the arithmetic system itself and
// checks the certificate against it.
//
// Properties: those of kernel_proof. A producer that proposes a certificate for
// a false goal is not a finding; the kernel accepting it is.

#include "cppl/testing/fuzz.hpp"
#include "cppl/testing/kernel_generator.hpp"
#include "cppl/testing/kernel_oracle.hpp"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace gen = cppl::testing::kernel_generator;
    gen::ByteChoices choices({data, size});
    const gen::Mode mode = choices.below(4) == 0 ? gen::Mode::Automation : gen::Mode::Arithmetic;
    const gen::Sample sample = gen::generate(choices, mode);
    cppl::testing::kernel_oracle::Statistics statistics;
    cppl::testing::fuzz::require_none(cppl::testing::kernel_oracle::examine(sample, statistics));
    return 0;
}
