// The kernel's term operations, from bytes (docs/KERNEL.md, "Substitution and
// shifting", "Normalization"; TRUST.md TCB-CORE-004, TCB-CORE-005).
//
// Every rule that closes a goal rests on these: reflexivity on normalization,
// universal elimination, transport and conditional elimination on
// substitution, hypothesis use on shifting.
//
// Properties, checked by evaluation in the independent model at every
// assignment of the binders it enumerates: normalization keeps a term's type
// and value and is idempotent; instantiating the innermost binder means
// evaluating with the argument's value there; shifting means inserting a binder
// no variable refers to. A budget refusal is not a finding.

#include "cppl/testing/fuzz.hpp"
#include "cppl/testing/kernel_generator.hpp"
#include "cppl/testing/kernel_oracle.hpp"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace gen = cppl::testing::kernel_generator;
    gen::ByteChoices choices({data, size});
    cppl::testing::kernel_oracle::Statistics statistics;
    cppl::testing::fuzz::require_none(cppl::testing::kernel_oracle::examine_terms(choices, statistics));
    return 0;
}
