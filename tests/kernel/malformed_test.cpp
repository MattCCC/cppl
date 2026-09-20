// Untrusted core inputs must be rejected without relying on frontend typing.
#include "cppl/kernel/check.hpp"
#include "cppl/testing/test.hpp"

#include <cstdint>
#include <limits>

namespace {
namespace k = cppl::kernel;
const auto u32 = k::Type::integer(32, k::Signedness::Unsigned);

k::Proposition identity() {
    const auto zero = k::Term::literal(u32.integer_type(), 0);
    return k::Proposition::equality(u32, zero, zero);
}
} // namespace

CPPL_TEST(every_quantifier_binder_is_validated_even_when_unused) {
    for (const std::uint16_t width : {std::uint16_t{0}, std::uint16_t{65}, std::numeric_limits<std::uint16_t>::max()}) {
        const auto invalid = k::Type::integer(width, k::Signedness::Unsigned);
        for (const bool used : {false, true}) {
            const auto variable = k::Term::variable(k::VarIndex{0});
            const auto body = used ? k::Proposition::equality(invalid, variable, variable) : identity();
            const auto goal = k::Proposition::for_all(invalid, body);
            const auto evidence = k::ProofTerm::forall_introduction(invalid, k::ProofTerm::reflexivity());
            const auto checked = k::check({}, goal, evidence, {});
            CPPL_CHECK(!checked.has_value());
            CPPL_CHECK(checked.error().kind == k::RejectionKind::MalformedProposition);
        }
    }
    for (const auto width : {1u, 32u, 64u}) {
        const auto valid = k::Type::integer(static_cast<std::uint16_t>(width), k::Signedness::Unsigned);
        CPPL_CHECK(k::check({}, k::Proposition::for_all(valid, identity()),
                            k::ProofTerm::forall_introduction(valid, k::ProofTerm::reflexivity()), {})
                       .has_value());
    }
}

CPPL_TEST(unknown_signedness_never_becomes_a_core_type) {
    for (unsigned tag = 2; tag <= 255; ++tag) {
        // Forging an out-of-range tag is the subject of the test: the kernel
        // must reject the type rather than assign it a meaning.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        const auto invalid = k::Type::integer(32, static_cast<k::Signedness>(tag));
        CPPL_CHECK(!k::is_supported(invalid.integer_type()));
        const auto zero = k::Term::literal(invalid.integer_type(), 0);
        CPPL_CHECK(
            !k::check({}, k::Proposition::equality(invalid, zero, zero), k::ProofTerm::reflexivity(), {}).has_value());
        CPPL_CHECK(!k::check({}, k::Proposition::for_all(invalid, identity()),
                             k::ProofTerm::forall_introduction(invalid, k::ProofTerm::reflexivity()), {})
                        .has_value());
        k::Context context;
        k::Definition definition{k::DefId{0}, "invalid", {invalid}, u32, k::Term::literal(u32.integer_type(), 0)};
        CPPL_CHECK(!context.define(definition).has_value());
        CPPL_CHECK_EQ(context.definition_count(), 0u);
    }
}

CPPL_TEST(a_free_variable_cannot_import_an_invalid_local_type) {
    // As above, the invalid signedness tag is deliberate.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto unknown_signedness = k::Type::integer(32, static_cast<k::Signedness>(255));
    for (const auto type : {k::Type::integer(0, k::Signedness::Unsigned), unknown_signedness}) {
        const auto result = k::type_of({}, {&type, 1}, k::Term::variable(k::VarIndex{0}));
        CPPL_CHECK(!result.has_value());
        CPPL_CHECK(result.error().kind == k::CoreErrorKind::MalformedType);
    }
}
