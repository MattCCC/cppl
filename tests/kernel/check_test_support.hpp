#pragma once

// The names, types and terms the kernel's checking tests (kernel_check_test)
// are written with.

#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <utility>

namespace check_test_detail {

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

inline const IntType kSigned32{32, Signedness::Signed};
inline const IntType kUnsigned32{32, Signedness::Unsigned};
inline const IntType kUnsigned8{8, Signedness::Unsigned};

inline Type signed32() {
    return Type::integer(32, Signedness::Signed);
}

inline Type unsigned32() {
    return Type::integer(32, Signedness::Unsigned);
}

inline Term bound() {
    return Term::variable(VarIndex{0});
}

inline ProofTerm introduce(Type binder) {
    return ProofTerm::forall_introduction(std::move(binder), ProofTerm::reflexivity());
}

// `identity(x) = x` over unsigned 32-bit integers.
inline cppl::kernel::Context with_identity() {
    cppl::kernel::Context context;
    Definition identity;
    identity.id = DefId{0};
    identity.name = "identity";
    identity.parameters = {unsigned32()};
    identity.result = unsigned32();
    identity.body = Term::variable(cppl::kernel::parameter_reference(1, 0));
    CPPL_CHECK(context.define(std::move(identity)).has_value());
    return context;
}

inline Term outer() {
    return Term::variable(VarIndex{1});
}

inline Term sum(Term lhs, Term rhs) {
    return Term::primitive(PrimOp::AddWrap, kUnsigned32, {std::move(lhs), std::move(rhs)});
}

using cppl::kernel::HypothesisIndex;

inline Term zero() {
    return Term::literal(kUnsigned32, 0);
}

// x = 0, stated of the innermost binder.
inline Proposition is_zero() {
    return Proposition::equality(unsigned32(), bound(), zero());
}

} // namespace check_test_detail
