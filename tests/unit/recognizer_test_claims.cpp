// Recognizer tests (unit_recognizer_test): contradiction claims and case
// splits in verified bodies.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/source/location.hpp"
#include "cppl/testing/test.hpp"
#include "recognizer_test_support.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

using namespace recognizer_test_detail;

// SPEC: VERIFIED-045, WORD-011
CPPL_TEST(a_contradiction_statement_in_a_verified_body_is_a_claim) {
    Recognized result;
    recognize("proof pinned(unsigned v) proves (v == v) { refl; }\n"
              "verified unsigned f(unsigned x) ensures (result == x) {\n"
              "    if (x > x) contradiction pinned(x);\n"
              "    return x;\n"
              "}\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.engine.diagnostics().empty());
    CPPL_CHECK_EQ(result.syntax.path_contradictions.size(), std::size_t{1});
    const auto& claim = result.syntax.path_contradictions[0];
    CPPL_CHECK_EQ(claim.function_index, std::size_t{0});
    CPPL_CHECK(claim.statement.kind == cppl::frontend::ProofStatementKind::Contradiction);
    CPPL_CHECK_EQ(claim.statement.reference, "pinned");
    CPPL_CHECK_EQ(claim.statement.arguments.size(), std::size_t{1});
    // What is erased stops short of the `;`, which the program keeps.
    CPPL_CHECK_EQ(claim.span.offset, claim.erased.offset);
    CPPL_CHECK_EQ(claim.span.length, claim.erased.length + 1);
    CPPL_CHECK_EQ(claim.statement.location.line, 3u);
    CPPL_CHECK_EQ(claim.statement.location.column, 16u);
}

// SPEC: WORD-002, WORD-011
CPPL_TEST(contradiction_named_anywhere_else_keeps_the_statement_ordinary_cpp) {
    // `contradiction v(x);` declares `v` wherever `contradiction` names a type.
    Recognized result;
    recognize("using contradiction = unsigned;\n"
              "verified unsigned f(unsigned x) ensures (result == x) { contradiction v(x); return v; }\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.path_contradictions.empty());
    CPPL_CHECK_EQ(result.engine.diagnostics().size(), std::size_t{1});
    CPPL_CHECK(result.engine.diagnostics()[0].severity == cppl::diagnostics::Severity::Warning);
}

// SPEC: WORD-002, WORD-011
CPPL_TEST(contradiction_inside_a_proof_leaves_the_word_to_cppl) {
    // The word's uses inside proofs are C++L's own, so they do not make a claim
    // in a verified body ordinary C++.
    Recognized result;
    recognize("law l(unsigned x) expects (x > x) proves (x == 0u) {\n"
              "    assume impossible : x > x;\n"
              "    contradiction impossible;\n"
              "}\n"
              "proof pinned(unsigned v) proves (v == v) { refl; }\n"
              "verified unsigned f(unsigned x) ensures (result == x) { if (x > x) { contradiction pinned(x); } "
              "return x; }\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.engine.diagnostics().empty());
    CPPL_CHECK_EQ(result.syntax.path_contradictions.size(), std::size_t{1});
}

// SPEC: VERIFIED-045, WORD-011
CPPL_TEST(a_claim_outside_a_verified_body_is_refused) {
    Recognized result;
    recognize("unsigned f(unsigned x) { if (x > x) { contradiction pinned; } return x; }\n", result);
    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.path_contradictions.empty());
}

// SPEC: WORD-002, WORD-011
CPPL_TEST(contradiction_where_no_statement_begins_is_not_a_claim) {
    // Inside a `for` header, and in a shape the statement never has: neither is
    // a claim, and each is left to C++ to accept or refuse.
    Recognized result;
    recognize("verified unsigned f(unsigned x) ensures (result == x) {\n"
              "    for (unsigned i = 0u; contradiction h; ++i) {}\n"
              "    contradiction = 3;\n"
              "    return x;\n"
              "}\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.path_contradictions.empty());
}

// SPEC: CASE-017, CASE-019, WORD-012
// A split in a verified body is one statement holding its arms whole: the claim
// in an arm and the omission are its own claims, recorded once each and marked
// as the split's, and neither is read again as a statement of the body.
CPPL_TEST(a_split_in_a_verified_body_holds_its_arms_claims_and_omissions) {
    Recognized result;
    recognize("proof pinned(unsigned v) proves (v == v) { refl; }\n"
              "verified void f(E& s) expects (s == E::a) {\n"
              "    cases s {\n"
              "        E::a => { cases s { E::a => {} unnamed(v) => { contradiction pinned(v); } } }\n"
              "        omit unnamed by contradiction pinned(0u);\n"
              "    }\n"
              "}\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.engine.diagnostics().empty());
    CPPL_CHECK_EQ(result.syntax.path_splits.size(), std::size_t{1});
    const auto& split = result.syntax.path_splits[0];
    CPPL_CHECK_EQ(split.function_index, std::size_t{0});
    CPPL_CHECK(split.statement.kind == cppl::frontend::ProofStatementKind::Cases);
    CPPL_CHECK_EQ(split.statement.arms.size(), std::size_t{2});
    CPPL_CHECK_EQ(split.statement.location.line, 3u);
    CPPL_CHECK_EQ(result.syntax.path_contradictions.size(), std::size_t{2});
    CPPL_CHECK_EQ(split.claims.size(), std::size_t{2});
    const auto& nested = result.syntax.path_contradictions[split.claims[0]];
    const auto& omission = result.syntax.path_contradictions[split.claims[1]];
    CPPL_CHECK(nested.split == std::optional<std::size_t>{0});
    CPPL_CHECK(!nested.omitted.has_value());
    CPPL_CHECK_EQ(nested.statement.location.line, 4u);
    CPPL_CHECK(omission.split == std::optional<std::size_t>{0});
    CPPL_CHECK(omission.omitted == std::optional<std::string>{"unnamed"});
    CPPL_CHECK_EQ(omission.statement.location.line, 5u);
}

// SPEC: WORD-011, WORD-012
// The word's uses inside a split are C++L's own, so they leave a claim outside
// the split a claim.
CPPL_TEST(contradiction_inside_a_split_leaves_the_word_to_cppl) {
    Recognized result;
    recognize("proof pinned(unsigned v) proves (v == v) { refl; }\n"
              "verified unsigned f(E s, unsigned x) ensures (result == x) {\n"
              "    cases s { E::a => { contradiction pinned(x); } unnamed(v) => {} }\n"
              "    if (x > x) contradiction pinned(x);\n"
              "    return x;\n"
              "}\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.engine.diagnostics().empty());
    CPPL_CHECK_EQ(result.syntax.path_splits.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.path_contradictions.size(), std::size_t{2});
}

// SPEC: WORD-012
// `cases c {3};` declares `c` wherever `cases` names a type.
CPPL_TEST(a_split_word_used_as_a_type_keeps_the_statement_ordinary_cpp) {
    Recognized result;
    recognize("struct cases { int v; };\n"
              "verified unsigned f(unsigned x) ensures (result == x) { cases c {3}; return x; }\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.path_splits.empty());
    CPPL_CHECK_EQ(result.engine.diagnostics().size(), std::size_t{1});
    CPPL_CHECK(result.engine.diagnostics()[0].severity == cppl::diagnostics::Severity::Warning);
}

// SPEC: WORD-012, CASE-019
CPPL_TEST(a_split_is_refused_outside_a_verified_body_or_with_a_goal_closing_arm) {
    Recognized outside;
    recognize("unsigned f(E s) { cases s { E::a => {} unnamed(v) => {} } return 0u; }\n", outside);
    CPPL_CHECK(outside.engine.has_errors());
    CPPL_CHECK(outside.syntax.path_splits.empty());

    Recognized closing;
    recognize("verified unsigned f(E s) ensures (result == 0u) { cases s { E::a => { refl; } unnamed(v) => {} } "
              "return 0u; }\n",
              closing);
    CPPL_CHECK(closing.engine.has_errors());
    CPPL_CHECK(closing.syntax.path_splits.empty());
}
