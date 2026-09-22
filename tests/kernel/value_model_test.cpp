#include "cppl/kernel/arithmetic.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/testing/test.hpp"

namespace {
namespace k = cppl::kernel;
const auto u32 = k::Type::integer(32, k::Signedness::Unsigned);
const auto object = k::Type::value("object", {k::Type{k::kBoolean}, u32});
const auto variable = k::Term::variable({0});
auto reflexive(const k::Type& domain, const k::Type& type, const k::Term& term) {
    return k::check({}, k::Proposition::for_all(domain, k::Proposition::equality(type, term, term)),
                    k::ProofTerm::forall_introduction(domain, k::ProofTerm::reflexivity()), {});
}
} // namespace

CPPL_TEST(abstract_values_and_projections_have_checked_types) {
    CPPL_CHECK(reflexive(object, object, variable));
    CPPL_CHECK(reflexive(object, u32, k::Term::project(object, 1, variable)));
    const auto nested = k::Type::value("nested", {object});
    const auto field = k::Term::project(nested, 0, variable);
    CPPL_CHECK(reflexive(nested, u32, k::Term::project(object, 1, field)));
}

CPPL_TEST(projection_corruption_fails_independently_of_frontend) {
    CPPL_CHECK(!reflexive(object, u32, k::Term::project(object, 0, variable)));
    CPPL_CHECK(!reflexive(object, u32, k::Term::project(object, 2, variable)));
    CPPL_CHECK(!reflexive(object, u32, k::Term::project(u32, 0, variable)));
    CPPL_CHECK(!reflexive(object, u32, k::Term::project(k::Type::value("other", {u32}), 0, variable)));
    CPPL_CHECK(!reflexive(object, u32, k::Term::project(k::Type::value("object", {u32}), 0, variable)));
    auto malformed = k::Term::project(object, 1, variable);
    std::get<k::Projection>(malformed.node).arguments.clear();
    CPPL_CHECK(!reflexive(object, u32, malformed));
    std::get<k::Projection>(malformed.node).arguments = {variable, variable};
    CPPL_CHECK(!reflexive(object, u32, malformed));
    CPPL_CHECK(!reflexive(k::Type::value(""), k::Type::value(""), variable));
}

CPPL_TEST(abstract_observations_do_not_manufacture_facts) {
    const auto tag = k::Term::project(object, 0, variable);
    CPPL_CHECK(!k::check({}, k::Proposition::for_all(object, k::predicate(tag, true)),
                         k::ProofTerm::forall_introduction(object, k::ProofTerm::reflexivity()), {}));
    const auto field = k::Term::project(object, 1, variable);
    const auto goal =
        k::Proposition::for_all(object, k::Proposition::equality(u32, field, k::Term::literal(u32.integer_type(), 42)));
    CPPL_CHECK(!k::check({}, goal, k::ProofTerm::forall_introduction(object, k::ProofTerm::reflexivity()), {}));
}

CPPL_TEST(projection_substitution_is_capture_safe_and_normalization_is_structural) {
    const auto field = k::Term::project(object, 1, variable);
    const auto shifted = k::Term::project(object, 1, k::Term::variable({2}));
    CPPL_CHECK_EQ(k::shift(field, 2), shifted);
    CPPL_CHECK_EQ(k::instantiate(field, k::Term::variable({2})), shifted);
    CPPL_CHECK_EQ(*k::normalize({}, field), field);
    CPPL_CHECK(k::compare(field, k::Term::project(object, 0, variable)) != 0);
    CPPL_CHECK(k::compare(field, field) == 0);
}

// Indexed observation (FOUNDATIONS.md 45). The index is a term, so these tests
// pin the properties a constant-position projection cannot express.
namespace {
const auto indexed = k::Type::indexed(u32, 4);
const auto zero = k::Term::literal(u32.integer_type(), 0);
const auto one = k::Term::literal(u32.integer_type(), 1);
} // namespace

CPPL_TEST(indexed_observations_have_checked_types) {
    CPPL_CHECK(reflexive(indexed, u32, k::Term::element(indexed, variable, zero)));
    // The index ranges over the whole integer type: an observation past the
    // extent is well-typed, because bounds are a separate obligation and not a
    // premise of term formation.
    CPPL_CHECK(reflexive(indexed, u32, k::Term::element(indexed, variable, k::Term::literal(u32.integer_type(), 99))));
    // An element of an abstract value type, and an indexed domain nested in one.
    const auto rows = k::Type::indexed(object, 2);
    CPPL_CHECK(reflexive(rows, u32, k::Term::project(object, 1, k::Term::element(rows, variable, zero))));
}

CPPL_TEST(indexed_observation_corruption_fails_independently_of_frontend) {
    // A domain that is not indexed, and one whose extent admits nothing.
    CPPL_CHECK(!reflexive(indexed, u32, k::Term::element(object, variable, zero)));
    CPPL_CHECK(!reflexive(indexed, u32, k::Term::element(u32, variable, zero)));
    CPPL_CHECK(!reflexive(k::Type::indexed(u32, 0), u32, k::Term::element(k::Type::indexed(u32, 0), variable, zero)));
    // The extent is part of type identity, so a subject of one extent does not
    // enter a domain of another.
    CPPL_CHECK(!reflexive(indexed, u32, k::Term::element(k::Type::indexed(u32, 8), variable, zero)));
    // The result type is the element type, not something else.
    CPPL_CHECK(!reflexive(indexed, k::Type{k::kBoolean}, k::Term::element(indexed, variable, zero)));
    // A non-integer index. The subject is correct and the index is well-typed
    // on its own, so only the index-type rule can reject this: under a domain
    // of `object` elements, an element observation yields an `object`, which is
    // a legal term but not a legal index.
    const auto objects = k::Type::indexed(object, 4);
    CPPL_CHECK(reflexive(objects, object, k::Term::element(objects, variable, zero)));
    CPPL_CHECK(
        !reflexive(objects, object, k::Term::element(objects, variable, k::Term::element(objects, variable, zero))));
    // Wrong arities.
    auto malformed = k::Term::element(indexed, variable, zero);
    std::get<k::Element>(malformed.node).arguments.clear();
    CPPL_CHECK(!reflexive(indexed, u32, malformed));
    std::get<k::Element>(malformed.node).arguments = {variable};
    CPPL_CHECK(!reflexive(indexed, u32, malformed));
    std::get<k::Element>(malformed.node).arguments = {variable, zero, zero};
    CPPL_CHECK(!reflexive(indexed, u32, malformed));
}

CPPL_TEST(indexed_observation_proves_no_bound_and_no_element_value) {
    // Forming the observation must not establish anything about the index, and
    // must not decide the observed value (SPEC.md STORAGE-012).
    const auto at = k::Term::element(indexed, variable, zero);
    const auto goal =
        k::Proposition::for_all(indexed, k::Proposition::equality(u32, at, k::Term::literal(u32.integer_type(), 42)));
    CPPL_CHECK(!k::check({}, goal, k::ProofTerm::forall_introduction(indexed, k::ProofTerm::reflexivity()), {}));
    CPPL_CHECK(!k::check({}, k::Proposition::for_all(indexed, k::predicate(at, true)),
                         k::ProofTerm::forall_introduction(indexed, k::ProofTerm::reflexivity()), {}));
}

CPPL_TEST(indexed_observation_admits_neither_injectivity_nor_extensionality) {
    // Two observations of one subject at different index terms are different
    // terms, and nothing proves them equal or unequal (SPEC.md STORAGE-013,
    // TRUST.md TCB-CORE-016).
    const auto first = k::Term::element(indexed, variable, zero);
    const auto second = k::Term::element(indexed, variable, one);
    CPPL_CHECK(k::compare(first, second) != 0);
    CPPL_CHECK(!k::check({}, k::Proposition::for_all(indexed, k::Proposition::equality(u32, first, second)),
                         k::ProofTerm::forall_introduction(indexed, k::ProofTerm::reflexivity()), {}));
    // Equal observations do not prove equal subjects: reflexivity closes a goal
    // only when the two sides are already the same term.
    CPPL_CHECK(k::compare(first, k::Term::element(indexed, variable, zero)) == 0);
    CPPL_CHECK(k::compare(first, k::Term::element(indexed, k::Term::variable({1}), zero)) != 0);
}

CPPL_TEST(indexed_observation_substitution_reaches_subject_and_index) {
    // Both operands are substituted: an index left untouched by instantiation
    // would bound a different term than the one the observation reads
    // (ARCHITECTURE.md ARCH-ELEM-004).
    const auto at = k::Term::element(indexed, variable, variable);
    const auto shifted = k::Term::element(indexed, k::Term::variable({2}), k::Term::variable({2}));
    CPPL_CHECK_EQ(k::shift(at, 2), shifted);
    CPPL_CHECK_EQ(k::instantiate(at, k::Term::variable({2})), shifted);
    CPPL_CHECK_EQ(*k::normalize({}, at), at);
    // Normalization is structural: the observation has no reduction rule, so it
    // never selects a component of anything.
    const auto constant = k::Term::element(indexed, variable, zero);
    CPPL_CHECK_EQ(*k::normalize({}, constant), constant);
}
