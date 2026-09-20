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
