#pragma once

// The generator of kernel derivations, and what the files that implement
// it share: the scope a derivation is built in and the rules it draws.

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/kernel_generator.hpp"
#include "cppl/testing/kernel_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cppl::testing::kernel_generator {

namespace k = kernel;
using kernel_model::Wide;

namespace detail {

// Defined in kernel_generator.cpp.
k::Type integer(std::uint16_t width, k::Signedness signedness);

inline const k::IntType kBoolean = k::kBoolean;

// Defined in kernel_generator.cpp.
k::Type value_v();
k::Type value_w();
k::Type indexed_a();

struct Hypothesis {
    k::Proposition proposition;
    std::size_t binders = 0;
    bool usable = true;
};

struct Scope {
    std::vector<k::Type> locals;
    std::vector<Hypothesis> hypotheses;
};

struct Derivation {
    k::Proposition proposition;
    k::ProofTerm proof;
};

enum class Rule : std::uint8_t {
    ReflexivitySame,
    ReflexivityEquivalent,
    ReflexivityRandom,
    Hypothesis,
    ForallIntroduction,
    ImplicationIntroduction,
    ForallElimination,
    ImplicationElimination,
    ConjunctionIntroduction,
    ConjunctionElimination,
    DisjunctionIntroduction,
    DisjunctionElimination,
    FalsityElimination,
    EqualityElimination,
    ConditionalElimination,
    LinearArithmetic,
    UnsignedInduction,
};
inline constexpr std::uint32_t kRuleCount = 17;

class Generator {
  public:
    explicit Generator(Choices& choices) : choices_(choices) {}

    Sample run(Mode mode);

  private:
    // ---- choices ------------------------------------------------------------

    std::uint32_t below(std::uint32_t bound);
    bool one_in(std::uint32_t odds);
    bool perturb();

    std::uint64_t bits64();

    // ---- types --------------------------------------------------------------

    k::IntType small_integer();

    k::IntType any_integer();

    // A type to quantify over: enumerable in the model, with abstract types and
    // (rarely) a type the model only samples.
    k::Type binder();

    static k::Type as_type(const k::IntType& type);

    // ---- terms --------------------------------------------------------------

    k::Term literal(const k::IntType& type);

    static std::vector<std::uint32_t> variables_of(std::span<const k::Type> locals, const k::Type& type);

    std::optional<k::Term> variable(std::span<const k::Type> locals, const k::Type& type);

    // A term of an abstract type, where one can be formed.
    std::optional<k::Term> abstract_term(std::span<const k::Type> locals, const k::Type& type, unsigned depth);

    std::optional<k::Term> call_of(std::span<const k::Type> locals, const k::Definition& definition, unsigned depth);

    std::optional<k::Term> term(std::span<const k::Type> locals, const k::Type& type, unsigned depth);

    k::Term integer_term(std::span<const k::Type> locals, const k::IntType& type, unsigned depth);

    // An observation of an abstract or indexed variable yielding `type`.
    std::optional<k::Term> observation(std::span<const k::Type> locals, const k::IntType& type, unsigned depth);

    k::Term comparison(std::span<const k::Type> locals, unsigned depth);

    // ---- propositions -------------------------------------------------------

    // A comparison stated as a proposition, in either of the forms the kernel
    // and the elaborator use.
    k::Proposition comparison_proposition(std::span<const k::Type> locals, unsigned depth);

    k::Proposition proposition(std::vector<k::Type>& locals, unsigned depth);

    // ---- definitions --------------------------------------------------------

    void define_some();

    // Offers a definition to the context, which admits it or not. A sample only
    // ever calls a definition the context holds, so one it refuses is dropped.
    void admit(k::Definition definition);

    // ---- rewriting that preserves meaning ----------------------------------

    std::optional<k::Type> type_of(std::span<const k::Type> locals, const k::Term& term) const;

    static k::Term substitute_parameters(const k::Term& body, const std::vector<k::Term>& arguments);

    // A term that denotes the same value as `term` on every assignment.
    k::Term equivalent(std::span<const k::Type> locals, const k::Term& term, unsigned depth);

    // ---- motives ------------------------------------------------------------

    // `term` with every occurrence of `target` replaced by the variable `hole`.
    static k::Term abstract(const k::Term& term, const k::Term& target, std::uint32_t hole);

    // A motive whose hole stands where `target` does in `proposition`. Both are
    // stated one binder out, under the hole; `depth` counts the quantifiers
    // passed inside the proposition.
    static k::Proposition abstract(const k::Proposition& proposition, const k::Term& target, std::uint32_t depth);

    // ---- scope --------------------------------------------------------------

    static k::Proposition available(const Scope& scope, std::size_t position);

    static k::ProofTerm use(const Scope& scope, std::size_t position);

    static std::size_t suppose(Scope& scope, k::Proposition premise, bool usable = true);

    // Closes every premise supposed since `mark` into an implication.
    static Derivation discharge(Scope& scope, std::size_t mark, Derivation derived);

    // A usable hypothesis whose restatement here satisfies `accepts`.
    template <typename Predicate> std::optional<std::size_t> find(const Scope& scope, Predicate accepts) {
        std::vector<std::size_t> found;
        for (std::size_t position = 0; position < scope.hypotheses.size(); ++position) {
            if (scope.hypotheses[position].usable && accepts(available(scope, position))) {
                found.push_back(position);
            }
        }
        if (found.empty()) {
            return std::nullopt;
        }
        return found[below(static_cast<std::uint32_t>(found.size()))];
    }

    // ---- derivations --------------------------------------------------------

    Derivation derive(Scope& scope, unsigned depth);

    // Induction over an unsigned type, whose two premises the kernel states
    // itself (kernel::induction_base, kernel::induction_step). A reflexive
    // motive has premises derivable anywhere, so the rule is accepted; any other
    // is a comparison whose premises automation proposes where the goal is
    // closed, so a motive false at some value is refused or, if the kernel ever
    // accepted it, false in the model. Perturbed, the rule names another binder,
    // the goal quantifies over a signed type, or the premises trade places.
    Derivation unsigned_induction(Scope& scope);

    // A claimed conclusion replaced by another, the evidence kept.
    Derivation misclaim(Scope& scope, Derivation derived);

    Derivation reflexivity(Scope& scope, unsigned kind);

    Derivation hypothesis(Scope& scope);

    Derivation forall_introduction(Scope& scope, unsigned depth);

    Derivation implication_introduction(Scope& scope, unsigned depth);

    // A term of `type` here, or nothing where none can be formed.
    std::optional<k::Term> argument(const Scope& scope, const k::Type& type);

    Derivation forall_elimination(Scope& scope, unsigned depth);

    // A capture-unsafe substitution, for a claim the kernel must refuse.
    static k::Term unshifted_instantiate(const k::Term& body, const k::Term& argument, std::uint32_t depth);

    static k::Proposition unshifted_instantiate(const k::Proposition& body, const k::Term& argument,
                                                std::uint32_t depth);

    Derivation implication_elimination(Scope& scope, unsigned depth);

    // Evidence for some proposition, either derived here or supposed; a
    // supposed one stays in scope until the caller discharges it.
    Derivation premise(Scope& scope, unsigned depth);

    // Evidence for a conjunction; premises it supposes stay in scope.
    Derivation conjunction_of(Scope& scope, unsigned depth);

    Derivation conjunction_introduction(Scope& scope, unsigned depth);

    Derivation conjunction_elimination(Scope& scope, unsigned depth);

    // Evidence for a disjunction by one of its sides; premises it supposes stay
    // in scope.
    Derivation disjunction_of(Scope& scope, unsigned depth);

    Derivation disjunction_introduction(Scope& scope, unsigned depth);

    Derivation disjunction_elimination(Scope& scope, unsigned depth);

    // Evidence for `False` from the premises in scope, where there is any.
    std::optional<k::ProofTerm> contradiction(Scope& scope);

    // Linear arithmetic over the equalities in scope, closing `goal`.
    std::optional<k::ProofTerm> arithmetic_from_scope(const Scope& scope, const k::Proposition& goal);

    std::optional<k::ArithmeticCertificate> propose_certificate(std::span<const k::Type> locals,
                                                                std::span<const k::Proposition> facts,
                                                                const k::Proposition& goal);

    k::ArithmeticCertificate random_certificate(std::size_t constraints, std::size_t variables,
                                                std::size_t disjunctions, unsigned depth);

    // A proposed certificate with one of its steps changed.
    k::ArithmeticCertificate corrupt(const k::ArithmeticCertificate& certificate, std::size_t constraints);

    Derivation falsity_elimination(Scope& scope, unsigned depth);

    Derivation equality_elimination(Scope& scope, unsigned depth);

    Derivation conditional_elimination(Scope& scope, unsigned depth);

    // Evidence for `premise -> goal`, where `premise` is an equality whose left
    // side, replaced by its right, turns `goal` into a reflexive equality.
    static k::ProofTerm restate(const Scope& /*scope*/, const k::Proposition& premise, const k::Proposition& goal);

    Derivation linear_arithmetic(Scope& scope);

    // ---- the arithmetic mode ------------------------------------------------

    // forall binders. F1 -> ... -> Fn -> G, closed by linear arithmetic.
    Derivation arithmetic();

    // ---- the automation mode ------------------------------------------------

    k::Proposition automation_goal();

  public:
    TermSample term_sample();

  private:
    Choices& choices_;
    k::Context context_;
};

} // namespace detail

} // namespace cppl::testing::kernel_generator
