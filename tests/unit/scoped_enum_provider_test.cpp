// The scoped-enumeration provider's state model.
//
// Only what is representation-specific belongs here: which cases exist, what
// their discriminators say, and which written label denotes which case. The
// generic machinery that consumes a partition is tested once, elsewhere.
#include "cppl/decomposition/decomposition.hpp"
#include "cppl/decomposition/labels.hpp"
#include "cppl/testing/test.hpp"

namespace {
namespace d = cppl::decomposition;
namespace v = cppl::vir;

v::Type enumeration(std::vector<v::Enumerator> enumerators, bool is_signed = true, std::uint16_t width = 32) {
    auto type = v::Type::integer(width, is_signed);
    type.representation.identity = "c:@E@State";
    type.representation.name = "State";
    type.representation.enumerators = std::move(enumerators);
    return type;
}
v::Expr subject(v::Type type) {
    v::Expr value;
    value.type = std::move(type);
    value.node = v::ParameterRef{0, "s"};
    return value;
}
d::SumDecomposition decompose(const v::Type& type) {
    return std::get<d::SumDecomposition>(d::decompose({subject(type), {}}));
}
// The constant a case's discriminator compares the subject against.
std::int64_t compared_value(const d::CaseDescriptor& descriptor) {
    const auto& comparison = std::get<v::Binary>(descriptor.discriminator.node);
    return std::get<v::IntLiteral>(comparison.operands[1].node).value;
}
v::Expr label(std::int64_t value, const v::Type& type) {
    v::Expr written;
    written.type = type;
    written.node = v::IntLiteral{value};
    return written;
}
} // namespace

CPPL_TEST(each_distinct_enumerator_value_is_one_case_in_declaration_order) {
    const auto type = enumeration({{"idle", -1}, {"running", 3}, {"unnamed", 8}});
    const auto sum = decompose(type);
    CPPL_CHECK_EQ(sum.cases.size(), std::size_t{3});
    CPPL_CHECK_EQ(sum.cases[0].label.text, std::string("State::idle"));
    CPPL_CHECK_EQ(compared_value(sum.cases[0]), std::int64_t{-1});
    CPPL_CHECK_EQ(compared_value(sum.cases[1]), std::int64_t{3});
    // An enumerator spelled like the reserved residual label is still an
    // ordinary named case; the reserved label is never an enumerator.
    CPPL_CHECK_EQ(sum.cases[2].label.text, std::string("State::unnamed"));
    CPPL_CHECK_EQ(compared_value(sum.cases[2]), std::int64_t{8});
}

CPPL_TEST(aliased_enumerators_denote_one_case_reached_by_either_name) {
    const auto type = enumeration({{"one", 1}, {"alias", 1}}, false);
    const auto sum = decompose(type);
    CPPL_CHECK_EQ(sum.cases.size(), std::size_t{1});
    CPPL_CHECK_EQ(sum.cases[0].label.text, std::string("State::one"));
    // Clang resolves either spelling to the same value, so either reaches the
    // same case and writing both is a duplicate.
    const auto& provider = *d::provider_for(type);
    const auto resolved = provider.resolve_label(sum, label(1, type));
    CPPL_CHECK(resolved.has_value());
    CPPL_CHECK_EQ(*resolved, std::size_t{0});
}

CPPL_TEST(the_value_set_is_the_underlying_type_so_a_residual_case_remains) {
    // An enum's states are its underlying type's values, not its enumerator
    // list, so the partition always carries a residual case.
    const auto sum = decompose(enumeration({{"a", 0}}));
    CPPL_CHECK(sum.exhaustiveness == d::ExhaustivenessModel::ResidualRequired);
    CPPL_CHECK_EQ(sum.residual.text, std::string("unnamed"));
    CPPL_CHECK_EQ(sum.residual_bindings.size(), std::size_t{1});
    // The binder aliases the subject at the underlying type: the same value,
    // with the representation forgotten. Nothing is created at runtime.
    CPPL_CHECK(sum.residual_bindings[0].kind == d::BindingKind::Alias);
    CPPL_CHECK(!sum.residual_bindings[0].type.representation.is_known());
    CPPL_CHECK(sum.residual_bindings[0].value.node == subject(enumeration({{"a", 0}})).node);
    // A named case binds nothing: an enumerator carries no payload.
    CPPL_CHECK(sum.cases[0].bindings.empty());
}

CPPL_TEST(an_enum_with_no_enumerators_has_only_its_residual_case) {
    const auto sum = decompose(enumeration({}, false));
    CPPL_CHECK(sum.cases.empty());
    CPPL_CHECK_EQ(sum.residual.text, std::string("unnamed"));
}

CPPL_TEST(a_label_of_another_enumeration_resolves_to_no_case) {
    const auto type = enumeration({{"a", 1}});
    const auto sum = decompose(type);
    const auto& provider = *d::provider_for(type);
    CPPL_CHECK(!provider.resolve_label(sum, label(2, type)).has_value());
}

CPPL_TEST(the_provider_selects_on_resolved_identity_not_on_a_spelling) {
    CPPL_CHECK(d::provider_for(enumeration({{"a", 1}})) != nullptr);
    // A plain machine integer is not an enumeration, whatever it is spelled.
    CPPL_CHECK(d::provider_for(v::Type::integer(32, true)) == nullptr);
    CPPL_CHECK(d::provider_for(v::Type::boolean()) == nullptr);
}

CPPL_TEST(the_residual_label_is_reserved_and_qualified_labels_are_expressions) {
    CPPL_CHECK(d::label_kind("unnamed") == d::LabelKind::Keyword);
    CPPL_CHECK(d::label_kind("State::unnamed") == d::LabelKind::Expression);
    CPPL_CHECK(d::label_kind("State::idle") == d::LabelKind::Expression);
}
