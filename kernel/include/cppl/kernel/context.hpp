#pragma once

#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace cppl::kernel {

enum class CoreErrorKind : std::uint8_t {
    DuplicateDefinition,
    UnknownDefinition,
    ArityMismatch,
    TypeMismatch,
    VariableOutOfScope,
    MalformedLiteral,
    MalformedPrimitive,
    MalformedType,
    DepthLimitExceeded,
    NormalizationBudgetExhausted,
};

std::string describe(CoreErrorKind kind);

struct CoreError {
    CoreErrorKind kind = CoreErrorKind::MalformedType;
    std::string detail;
};

// A definition names a total first-order function over machine integers.
//
// `name` is diagnostic text only. Semantic identity is `id`.
//
// Inside `body`, the parameters are the enclosing binders: parameter 0 is the
// outermost one, so it carries the largest de Bruijn index
// (see parameter_reference()).
struct Definition {
    DefId id;
    std::string name;
    std::vector<Type> parameters;
    Type result;
    Term body;
};

// The set of definitions a proposition may mention.
//
// A definition may only call definitions already present, so the call graph is
// acyclic by construction. That is what makes normalization terminate: this
// core admits no recursion, and divergence therefore cannot manufacture
// evidence (SPEC.md 22, AGENTS.md 9).
class Context {
  public:
    // Admits a definition after checking it is well formed and well typed.
    // A malformed definition is rejected; nothing is added.
    [[nodiscard]] std::expected<void, CoreError> define(Definition definition);

    [[nodiscard]] const Definition* lookup(DefId id) const noexcept;

    [[nodiscard]] std::size_t definition_count() const noexcept {
        return definitions_.size();
    }

    [[nodiscard]] const std::vector<Definition>& definitions() const noexcept {
        return definitions_;
    }

  private:
    std::vector<Definition> definitions_;
};

// Structural limits applied to untrusted formal input.
struct CoreLimits {
    std::uint64_t max_normalization_steps = 1u << 20;
    std::uint32_t max_term_depth = 512;
    // Monomials in one polynomial, and factors in one monomial. A rendered
    // polynomial nests about as deep as the two together, so they stay well
    // inside max_term_depth.
    std::uint32_t max_polynomial_terms = 256;
    std::uint32_t max_monomial_degree = 64;
    // Facts one arithmetic step may use, and nodes in its certificate.
    std::uint32_t max_arithmetic_facts = 256;
    std::uint32_t max_certificate_nodes = 1u << 14;
};

// The type of `term` under `locals`, which lists enclosing binders
// outermost-first: Var{0} refers to locals.back().
[[nodiscard]] std::expected<Type, CoreError> type_of(const Context& context, std::span<const Type> locals,
                                                     const Term& term, const CoreLimits& limits = {});

// Reduction to normal form: definitions are unfolded and primitive operations
// on literals are folded. Free variables are opaque. Deterministic.
[[nodiscard]] std::expected<Term, CoreError> normalize(const Context& context, const Term& term,
                                                       const CoreLimits& limits = {});

std::string describe(const Context& context, const Term& term);

} // namespace cppl::kernel
