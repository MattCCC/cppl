#pragma once

// The integer types, terms, propositions and automation entry points the
// arithmetic evidence tests (unit_arithmetic_test) are written with.

#include "cppl/automation/evidence.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace arithmetic_test_detail {

namespace k = cppl::kernel;

inline const k::IntType kU1{1, k::Signedness::Unsigned};
inline const k::IntType kI1{1, k::Signedness::Signed};
inline const k::IntType kU2{2, k::Signedness::Unsigned};

inline const k::IntType kU8{8, k::Signedness::Unsigned};
inline const k::IntType kI8{8, k::Signedness::Signed};
inline const k::IntType kU16{16, k::Signedness::Unsigned};

inline const k::IntType kU32{32, k::Signedness::Unsigned};
inline const k::IntType kI32{32, k::Signedness::Signed};
inline const k::IntType kU63{63, k::Signedness::Unsigned};

inline const k::IntType kU64{64, k::Signedness::Unsigned};
inline const k::IntType kI64{64, k::Signedness::Signed};

inline k::Type type(const k::IntType& integer) {
    return k::Type{integer};
}

inline k::Term var(std::uint32_t index) {
    return k::Term::variable(k::VarIndex{index});
}

inline k::Term lit(const k::IntType& integer, std::int64_t value) {
    return k::Term::literal(integer, value);
}

inline k::Term binary(k::PrimOp op, const k::IntType& integer, k::Term a, k::Term b) {
    return k::Term::primitive(op, integer, {std::move(a), std::move(b)});
}

inline k::Term add(const k::IntType& integer, k::Term a, k::Term b) {
    return binary(k::PrimOp::AddWrap, integer, std::move(a), std::move(b));
}

inline k::Term sub(const k::IntType& integer, k::Term a, k::Term b) {
    return binary(k::PrimOp::SubWrap, integer, std::move(a), std::move(b));
}

inline k::Term mul(const k::IntType& integer, k::Term a, k::Term b) {
    return binary(k::PrimOp::MulWrap, integer, std::move(a), std::move(b));
}

inline k::Term select(const k::IntType& result_type, k::Term condition, k::Term when_true, k::Term when_false) {
    return k::Term::primitive(k::PrimOp::Select, result_type,
                              {std::move(condition), std::move(when_true), std::move(when_false)});
}

inline k::Term compare(k::PrimOp op, const k::IntType& integer, k::Term a, k::Term b) {
    return binary(op, integer, std::move(a), std::move(b));
}

inline k::Proposition holds(k::PrimOp op, const k::IntType& integer, k::Term a, k::Term b, bool value = true) {
    return k::Proposition::equality(type(k::kBoolean), compare(op, integer, std::move(a), std::move(b)),
                                    lit(k::kBoolean, value ? 1 : 0));
}

inline k::Proposition equal(const k::IntType& integer, k::Term a, k::Term b) {
    return k::Proposition::equality(type(integer), std::move(a), std::move(b));
}

inline k::Proposition imply_all(std::vector<k::Proposition> premises, k::Proposition conclusion) {
    for (auto& premise : std::views::reverse(premises)) {
        conclusion = k::Proposition::implication(premise, std::move(conclusion));
    }
    return conclusion;
}

// `binders` are outermost-first. Var{0} denotes binders.back().
inline k::Proposition quantify(std::vector<k::Type> binders, k::Proposition body) {
    for (auto& binder : std::views::reverse(binders)) {
        body = k::Proposition::for_all(binder, std::move(body));
    }
    return body;
}

inline k::Proposition closed(const k::IntType& integer, std::size_t binders, std::vector<k::Proposition> premises,
                             k::Proposition conclusion) {
    conclusion = imply_all(std::move(premises), std::move(conclusion));
    for (std::size_t index = 0; index < binders; ++index) {
        conclusion = k::Proposition::for_all(type(integer), std::move(conclusion));
    }
    return conclusion;
}

inline std::optional<cppl::automation::Evidence> proposal(const k::Context& context, const k::Proposition& goal) {
    return cppl::automation::propose(context, goal);
}

inline bool accepted(const k::Context& context, const k::Proposition& goal, const k::ProofTerm& proof,
                     const k::CoreLimits& limits = {}) {
    return k::check(context, goal, proof, limits).has_value();
}

inline bool proven(const k::Context& context, const k::Proposition& goal) {
    const auto evidence = proposal(context, goal);
    return evidence.has_value() && accepted(context, goal, evidence->proof);
}

inline bool proven(const k::Proposition& goal) {
    const k::Context context;
    return proven(context, goal);
}

// A sound automation layer is allowed to return no evidence for a false goal,
// but if it returns anything, the kernel must reject it.
inline bool not_accepted_from_automation(const k::Context& context, const k::Proposition& goal) {
    const auto evidence = proposal(context, goal);
    return !evidence.has_value() || !accepted(context, goal, evidence->proof);
}

inline bool not_accepted_from_automation(const k::Proposition& goal) {
    const k::Context context;
    return not_accepted_from_automation(context, goal);
}

inline std::optional<k::Wide> normalized_literal(const k::Context& context, const k::Term& term,
                                                 const k::CoreLimits& limits = {}) {
    const auto normalized = k::normalize(context, term, limits);
    if (!normalized.has_value()) {
        return std::nullopt;
    }
    const auto* literal = std::get_if<k::Literal>(&normalized->node);
    if (literal == nullptr) {
        return std::nullopt;
    }
    return literal->value;
}

inline std::optional<k::Wide> normalized_literal(const k::Term& term) {
    const k::Context context;
    return normalized_literal(context, term);
}

inline k::ArithmeticCertificate empty_farkas() {
    return k::ArithmeticCertificate{k::FarkasSum{}};
}

inline k::ArithmeticCertificate impossible_multiplier_certificate() {
    return k::ArithmeticCertificate{k::FarkasSum{{std::pair<std::uint32_t, k::Wide>{999999u, k::Wide{1}}}}};
}

inline k::Definition unary_definition(k::DefId id, std::string name, const k::IntType& argument_type,
                                      const k::IntType& result_type, k::Term body) {
    return k::Definition{id, std::move(name), {type(argument_type)}, type(result_type), std::move(body)};
}

inline k::Wide pow2(unsigned width) {
    return k::Wide{1} << width;
}

} // namespace arithmetic_test_detail
