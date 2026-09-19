#pragma once

#include <compare>
#include <cstdint>
#include <expected>
#include <vector>

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"

namespace cppl::kernel {

// A total order on terms, decided by structure alone. Wherever the kernel must
// choose one arrangement of equal things, it chooses by this order, so the
// choice never depends on addresses, hashing or construction history.
[[nodiscard]] std::strong_ordering compare(const Term& lhs, const Term& rhs);

// One term of a polynomial: `coefficient` times the product of `factors`.
//
// A factor is a normal term of the polynomial's type that is not itself ring
// arithmetic: a variable, a comparison, a selection. Factors are kept sorted by
// compare(), with repetition for powers. The coefficient is a residue modulo
// 2^width and is never zero.
struct Monomial {
    std::vector<Term> factors;
    std::uint64_t coefficient = 0;

    friend bool operator==(const Monomial&, const Monomial&) = default;
};

// An element of the ring of integers modulo 2^width, over opaque factors.
//
// Two's-complement addition, subtraction and multiplication of `type` are that
// ring's operations whatever the signedness, so every identity of a commutative
// ring holds of them exactly. Monomials are distinct and ordered by degree,
// then by their factors; `constant` is a residue.
struct Polynomial {
    IntType type;
    std::vector<Monomial> monomials;
    std::uint64_t constant = 0;

    friend bool operator==(const Polynomial&, const Polynomial&) = default;
};

// Reads a term already in normal form as a polynomial of `type`.
[[nodiscard]] std::expected<Polynomial, CoreError> polynomial(const Term& normal,
                                                              IntType type,
                                                              const CoreLimits& limits);

// The canonical term denoting `polynomial`. Reading it back yields the same
// polynomial, which is what makes normalization idempotent.
[[nodiscard]] Term render(const Polynomial& polynomial);

// The product of `monomial`'s factors alone, as the term the rendering uses.
[[nodiscard]] Term render_factors(const IntType& type, const std::vector<Term>& factors);

// The normal form of a primitive whose operands are already normal
// (SPEC.md 7.1). Arithmetic is put in polynomial normal form; comparisons,
// negation and selection are put in canonical form and folded where their value
// is determined. Every rewrite is an identity of the machine semantics.
[[nodiscard]] std::expected<Term, CoreError> normalize_primitive(PrimOp op,
                                                                 IntType type,
                                                                 std::vector<Term> operands,
                                                                 const CoreLimits& limits,
                                                                 std::uint64_t& steps);

// The value `bits` denotes in `type`, read as an integer.
[[nodiscard]] Wide value_of(const IntType& type, std::uint64_t bits);

// The residue of `value` modulo 2^width, as the bits of `type`.
[[nodiscard]] std::uint64_t bits_of(const IntType& type, std::int64_t value);

}  // namespace cppl::kernel
