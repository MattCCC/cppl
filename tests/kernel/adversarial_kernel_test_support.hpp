#pragma once

// The types and terms the adversarial kernel tests
// (kernel_adversarial_test) build their attacks from.

#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"

#include <cstdint>

namespace adversarial_kernel_test_detail {

using cppl::kernel::CoreLimits;
using cppl::kernel::DefId;
using cppl::kernel::Definition;
using cppl::kernel::IntType;
using cppl::kernel::PrimOp;
using cppl::kernel::ProofTerm;
using cppl::kernel::Proposition;
using cppl::kernel::RejectionKind;
using cppl::kernel::Signedness;
using cppl::kernel::Term;
using cppl::kernel::Type;
using cppl::kernel::VarIndex;

inline const IntType kU32{32, Signedness::Unsigned};
inline const IntType kU8{8, Signedness::Unsigned};

inline Type u32() {
    return Type::integer(32, Signedness::Unsigned);
}
inline Type u8() {
    return Type::integer(8, Signedness::Unsigned);
}

inline Term zero32() {
    return Term::literal(kU32, 0);
}
inline Term one32() {
    return Term::literal(kU32, 1);
}
inline Term two32() {
    return Term::literal(kU32, 2);
}

inline Term literal(std::int64_t val) {
    return Term::literal(kU32, val);
}

inline Term var0() {
    return Term::variable(VarIndex{0});
}

inline Proposition eq(std::int64_t lhs, std::int64_t rhs) {
    return Proposition::equality(u32(), literal(lhs), literal(rhs));
}

} // namespace adversarial_kernel_test_detail
