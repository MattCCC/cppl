// Case-site lookup, missing-case completion and hover over the states the
// compiler's case engine recorded.
//
// These drive the view directly with a hand-built syntax tree and a hand-built
// record, so they check the view's own logic -- which site encloses an offset,
// which states are still unwritten, how a state is presented -- without
// depending on Clang being present. `lsp_fixtures_test` covers the real
// pipeline end to end.

#include "cppl/lsp/decomposition_view.hpp"
#include "cppl/testing/test.hpp"

#include <string>
#include <vector>

using namespace cppl::lsp;
using cppl::elaboration::SubjectStates;
using cppl::frontend::ProofArm;
using cppl::frontend::ProofDeclaration;
using cppl::frontend::ProofStatement;
using cppl::frontend::ProofStatementKind;
using cppl::frontend::Syntax;

namespace {

cppl::source::SourceLocation location_at(std::uint32_t line) {
    return {"proof.cpp", line, 5};
}

// `cases v { ... }` whose arm block spans [offset, offset + length).
ProofStatement cases_statement(std::size_t offset, std::size_t length, std::uint32_t line,
                               const std::vector<std::string>& written_labels) {
    ProofStatement statement;
    statement.kind = ProofStatementKind::Cases;
    statement.reference = "v";
    statement.location = location_at(line);
    statement.arms_span = {offset, length};
    for (const std::string& label : written_labels) {
        ProofArm arm;
        arm.spelling = label;
        arm.location = location_at(line);
        statement.arms.push_back(std::move(arm));
    }
    return statement;
}

// The three states of a two-alternative variant: the named alternatives plus
// the residual `valueless` the engine derives.
SubjectStates variant_record(std::uint32_t line) {
    SubjectStates record;
    record.location = location_at(line);
    record.subject = "v";
    record.representation = "std::variant<int, bool>";
    record.provider = "resolved structural values";
    record.states.push_back({"alternative<0>", {"value"}, false});
    record.states.push_back({"alternative<1>", {"value"}, false});
    record.states.push_back({"valueless", {}, true});
    return record;
}

Syntax syntax_with(std::vector<ProofStatement> statements) {
    ProofDeclaration proof;
    proof.name = "p";
    proof.statements = std::move(statements);
    Syntax syntax;
    syntax.proofs.push_back(std::move(proof));
    return syntax;
}

} // namespace

CPPL_TEST(case_site_found_inside_arm_block) {
    const Syntax syntax = syntax_with({cases_statement(100, 50, 7, {})});
    const std::vector<SubjectStates> recorded{variant_record(7)};

    CPPL_CHECK(enclosing_case_site(syntax, recorded, 120).has_value());
    // Both braces count as inside: completing right after typing '{' is the
    // common case.
    CPPL_CHECK(enclosing_case_site(syntax, recorded, 100).has_value());
    CPPL_CHECK(enclosing_case_site(syntax, recorded, 150).has_value());
}

CPPL_TEST(case_site_absent_outside_arm_block) {
    const Syntax syntax = syntax_with({cases_statement(100, 50, 7, {})});
    const std::vector<SubjectStates> recorded{variant_record(7)};

    CPPL_CHECK(!enclosing_case_site(syntax, recorded, 99).has_value());
    CPPL_CHECK(!enclosing_case_site(syntax, recorded, 151).has_value());
}

CPPL_TEST(case_site_resolves_innermost_when_nested) {
    // An outer `cases` spanning [100, 200) whose arm holds an inner one
    // spanning [130, 160). An offset in the inner block belongs to the inner
    // site: that is the block the author is completing in.
    ProofStatement outer = cases_statement(100, 100, 7, {});
    ProofArm arm;
    arm.spelling = "alternative<0>";
    arm.location = location_at(8);
    arm.statements.push_back(cases_statement(130, 30, 9, {}));
    outer.arms.push_back(std::move(arm));

    const Syntax syntax = syntax_with({std::move(outer)});
    std::vector<SubjectStates> recorded{variant_record(7), variant_record(9)};
    recorded[1].subject = "inner";

    const auto inner_site = enclosing_case_site(syntax, recorded, 140);
    CPPL_CHECK(inner_site.has_value());
    CPPL_CHECK(inner_site->states != nullptr);
    CPPL_CHECK(inner_site->states->subject == "inner");

    const auto outer_site = enclosing_case_site(syntax, recorded, 110);
    CPPL_CHECK(outer_site.has_value());
    CPPL_CHECK(outer_site->states != nullptr);
    CPPL_CHECK(outer_site->states->subject == "v");
}

CPPL_TEST(completion_offers_every_unwritten_state) {
    const Syntax syntax = syntax_with({cases_statement(100, 50, 7, {})});
    const std::vector<SubjectStates> recorded{variant_record(7)};
    const auto site = enclosing_case_site(syntax, recorded, 120);
    CPPL_CHECK(site.has_value());

    const std::vector<CompletionItem> items = missing_case_completions(*site);
    CPPL_CHECK(items.size() == 3);
    CPPL_CHECK(items[0].label == "alternative<0>");
    CPPL_CHECK(items[1].label == "alternative<1>");
    CPPL_CHECK(items[2].label == "valueless");
    // The provider's order is preserved, so the editor does not reorder the
    // partition into something arbitrary.
    CPPL_CHECK(items[0].sortText < items[1].sortText);
    CPPL_CHECK(items[1].sortText < items[2].sortText);
}

CPPL_TEST(completion_omits_states_already_written) {
    const Syntax syntax = syntax_with({cases_statement(100, 50, 7, {"alternative<0>", "valueless"})});
    const std::vector<SubjectStates> recorded{variant_record(7)};
    const auto site = enclosing_case_site(syntax, recorded, 120);
    CPPL_CHECK(site.has_value());

    const std::vector<CompletionItem> items = missing_case_completions(*site);
    CPPL_CHECK(items.size() == 1);
    CPPL_CHECK(items[0].label == "alternative<1>");
}

CPPL_TEST(completion_inserts_arm_with_provider_binders) {
    const Syntax syntax = syntax_with({cases_statement(100, 50, 7, {})});
    const std::vector<SubjectStates> recorded{variant_record(7)};
    const auto site = enclosing_case_site(syntax, recorded, 120);
    CPPL_CHECK(site.has_value());

    const std::vector<CompletionItem> items = missing_case_completions(*site);
    CPPL_CHECK(items.size() == 3);
    // A payload state inserts its binder; the residual carries none here, so
    // it inserts no binder list rather than an empty one.
    CPPL_CHECK(items[0].insertText.starts_with("alternative<0>(value) => {"));
    CPPL_CHECK(items[2].insertText.starts_with("valueless => {"));
}

CPPL_TEST(completion_is_empty_without_a_compiler_record) {
    // No record for this statement: the pipeline never reached elaboration,
    // or no provider models the subject. Offering nothing is the whole point
    // -- the server must not invent states the compiler did not confirm.
    const Syntax syntax = syntax_with({cases_statement(100, 50, 7, {})});
    const std::vector<SubjectStates> none;
    const auto site = enclosing_case_site(syntax, none, 120);
    CPPL_CHECK(site.has_value());
    CPPL_CHECK(site->states == nullptr);
    CPPL_CHECK(missing_case_completions(*site).empty());
    CPPL_CHECK(!case_site_hover(*site).has_value());
}

CPPL_TEST(hover_marks_written_and_residual_states) {
    const Syntax syntax = syntax_with({cases_statement(100, 50, 7, {"alternative<0>"})});
    const std::vector<SubjectStates> recorded{variant_record(7)};
    const auto site = enclosing_case_site(syntax, recorded, 120);
    CPPL_CHECK(site.has_value());

    const std::optional<Hover> hover = case_site_hover(*site);
    CPPL_CHECK(hover.has_value());
    CPPL_CHECK(hover->contents.find("std::variant<int, bool>") != std::string::npos);
    // The written arm is checked off and the unwritten ones are not, so the
    // author can see what is still owed.
    CPPL_CHECK(hover->contents.find("- [x] `alternative<0>(value)`") != std::string::npos);
    CPPL_CHECK(hover->contents.find("- [ ] `alternative<1>(value)`") != std::string::npos);
    CPPL_CHECK(hover->contents.find("- [ ] `valueless` — residual") != std::string::npos);
}

// SPEC: CASE-004
CPPL_TEST(omitted_state_is_accounted_for_but_not_presented_as_an_arm) {
    // `omit valueless by contradiction e;` accounts for the state (CASE-004),
    // so completion must not offer it again. It is accounted for by a claim
    // that the state cannot occur, not by an arm, and the hover says so. The
    // hover is built from syntax alone, so it must not present that claim as
    // checked: an omission whose evidence fails hovers exactly the same way.
    ProofStatement statement = cases_statement(100, 50, 7, {"alternative<0>"});
    ProofArm omission;
    omission.spelling = "valueless";
    omission.location = location_at(7);
    omission.omitted = true;
    statement.arms.push_back(std::move(omission));
    const Syntax syntax = syntax_with({std::move(statement)});
    const std::vector<SubjectStates> recorded{variant_record(7)};
    const auto site = enclosing_case_site(syntax, recorded, 120);
    CPPL_CHECK(site.has_value());

    const std::vector<CompletionItem> items = missing_case_completions(*site);
    CPPL_CHECK(items.size() == 1);
    CPPL_CHECK(items[0].label == "alternative<1>");

    const std::optional<Hover> hover = case_site_hover(*site);
    CPPL_CHECK(hover.has_value());
    CPPL_CHECK(hover->contents.find("- [x] `valueless` — residual — omitted, claimed impossible by contradiction") !=
               std::string::npos);
    CPPL_CHECK(hover->contents.find("shown") == std::string::npos);
    CPPL_CHECK(hover->contents.find("proven") == std::string::npos);
    // An ordinary arm is not described as omitted.
    CPPL_CHECK(hover->contents.find("`alternative<0>(value)` — omitted") == std::string::npos);
}

CPPL_TEST(product_hover_names_components_not_alternatives) {
    // A product is not a sum. Its one `components(...)` arm must not be
    // presented as a state partition (`AGENTS.md` 39).
    SubjectStates record;
    record.location = location_at(7);
    record.subject = "p";
    record.representation = "std::pair<int, bool>";
    record.provider = "resolved structural values";
    record.product = true;
    record.states.push_back({"components", {"first", "second"}, false});

    ProofStatement statement = cases_statement(100, 50, 7, {});
    statement.kind = ProofStatementKind::Decompose;
    const Syntax syntax = syntax_with({std::move(statement)});
    const std::vector<SubjectStates> recorded{record};
    const auto site = enclosing_case_site(syntax, recorded, 120);
    CPPL_CHECK(site.has_value());

    const std::optional<Hover> hover = case_site_hover(*site);
    CPPL_CHECK(hover.has_value());
    CPPL_CHECK(hover->contents.find("Product decomposition") != std::string::npos);
    CPPL_CHECK(hover->contents.find("components(first, second)") != std::string::npos);
    CPPL_CHECK(hover->contents.find("residual") == std::string::npos);
}
