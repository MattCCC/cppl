// Recognizing text still being written (RecognitionMode::Draft), and what C++L
// admits where it is being written (compiler/frontend/include/cppl/frontend/
// admissible.hpp). An editor reads both from here and never reads C++L's
// grammar itself.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/admissible.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace cppl::frontend;

namespace {

// A text, lexed and recognized, kept together because the tokens refer into
// the text.
struct Recognized {
    std::string text;
    std::unique_ptr<TokenStream> stream;
    Syntax syntax;
    std::size_t cursor = 0;
};

std::unique_ptr<Recognized> recognized(std::string text, RecognitionMode mode = RecognitionMode::Draft) {
    auto result = std::make_unique<Recognized>();
    const std::size_t cursor = text.find('|');
    if (cursor != std::string::npos) {
        text.erase(cursor, 1);
        result->cursor = cursor;
    }
    result->text = std::move(text);
    result->stream = std::make_unique<TokenStream>(lex(result->text, "main.cpp"));
    cppl::diagnostics::Engine engine;
    result->syntax = recognize(*result->stream, engine, mode);
    return result;
}

Admissible admissible(const Recognized& draft) {
    return admissible_at(*draft.stream, draft.syntax, draft.cursor);
}

std::string spelled(const Recognized& draft, const cppl::source::ByteSpan& span) {
    return draft.text.substr(span.offset, span.length);
}

std::string names(const std::vector<Evidence>& evidence) {
    std::string all;
    for (const Evidence& item : evidence) {
        all += (all.empty() ? "" : " ") + item.name;
    }
    return all;
}

bool admits(const Admissible& here, ClauseKind kind) {
    return std::ranges::find(here.clauses, kind) != here.clauses.end();
}

} // namespace

CPPL_TEST(each_statement_word_is_the_inverse_of_describe) {
    for (const ProofStatementKind kind :
         {ProofStatementKind::Reflexivity, ProofStatementKind::Exact, ProofStatementKind::Apply,
          ProofStatementKind::Assume, ProofStatementKind::Rewrite, ProofStatementKind::Contradiction,
          ProofStatementKind::Cases, ProofStatementKind::Decompose, ProofStatementKind::Induction}) {
        CPPL_CHECK(statement_keyword(describe(kind)) == kind);
    }
    CPPL_CHECK(!statement_keyword("unread").has_value());
    CPPL_CHECK(!statement_keyword("proof").has_value());
    CPPL_CHECK(names_evidence(ProofStatementKind::Exact));
    CPPL_CHECK(names_evidence(ProofStatementKind::Contradiction));
    CPPL_CHECK(!names_evidence(ProofStatementKind::Assume));
    CPPL_CHECK(!names_evidence(ProofStatementKind::Reflexivity));
}

CPPL_TEST(names_and_bodies_are_recognized_where_they_are_written) {
    const auto draft = recognized("law holds(int x)\n    proves (x == x);\n"
                                  "proof holds_twice(int x)\n    proves (holds(x))\n{\n    exact holds(x);\n}\n"
                                  "type Small = int where (self < 10);\n",
                                  RecognitionMode::Compile);
    CPPL_CHECK_EQ(draft->syntax.laws.size(), std::size_t{1});
    CPPL_CHECK_EQ(draft->syntax.proofs.size(), std::size_t{1});
    CPPL_CHECK_EQ(draft->syntax.refinement_types.size(), std::size_t{1});
    if (draft->syntax.proofs.size() != 1 || draft->syntax.laws.size() != 1 ||
        draft->syntax.refinement_types.size() != 1) {
        return;
    }
    CPPL_CHECK_EQ(spelled(*draft, draft->syntax.laws[0].name_span), std::string("holds"));
    const ProofDeclaration& proof = draft->syntax.proofs[0];
    CPPL_CHECK_EQ(spelled(*draft, proof.name_span), std::string("holds_twice"));
    CPPL_CHECK_EQ(spelled(*draft, proof.body), std::string("{\n    exact holds(x);\n}"));
    CPPL_CHECK(proof.completeness == Completeness::Whole);
    CPPL_CHECK_EQ(proof.statements.size(), std::size_t{1});
    if (!proof.statements.empty()) {
        CPPL_CHECK_EQ(spelled(*draft, proof.statements[0].span), std::string("exact holds(x);"));
    }
    CPPL_CHECK_EQ(spelled(*draft, draft->syntax.refinement_types[0].name_span), std::string("Small"));
}

CPPL_TEST(a_statement_that_closes_arms_spans_through_them) {
    const auto draft = recognized("proof split(int x)\n    proves (x == x)\n{\n"
                                  "    cases x {\n        State::one => { refl; }\n    }\n    refl;\n}\n",
                                  RecognitionMode::Compile);
    CPPL_CHECK_EQ(draft->syntax.proofs.size(), std::size_t{1});
    if (draft->syntax.proofs.size() == 1 && draft->syntax.proofs[0].statements.size() == 2) {
        const ProofStatement& cases = draft->syntax.proofs[0].statements[0];
        CPPL_CHECK_EQ(spelled(*draft, cases.span), std::string("cases x {\n        State::one => { refl; }\n    }"));
        CPPL_CHECK_EQ(spelled(*draft, cases.arms.at(0).statements.at(0).span), std::string("refl;"));
    }
}

CPPL_TEST(only_a_draft_keeps_a_law_or_a_proof_not_yet_written_whole) {
    const std::string heads = "law pending(int x)\nint between = 0;\nproof owed(int x)\nint after = 0;\n";
    for (const RecognitionMode mode : {RecognitionMode::Compile, RecognitionMode::Edit}) {
        const auto finished = recognized(heads, mode);
        CPPL_CHECK(finished->syntax.laws.empty());
        CPPL_CHECK(finished->syntax.proofs.empty());
    }
    const auto draft = recognized(heads);
    CPPL_CHECK_EQ(draft->syntax.laws.size(), std::size_t{1});
    CPPL_CHECK_EQ(draft->syntax.proofs.size(), std::size_t{1});
    if (!draft->syntax.laws.empty() && !draft->syntax.proofs.empty()) {
        CPPL_CHECK(draft->syntax.laws[0].completeness == Completeness::AwaitingClause);
        CPPL_CHECK_EQ(draft->syntax.laws[0].name, std::string("pending"));
        CPPL_CHECK_EQ(spelled(*draft, draft->syntax.laws[0].range.span), std::string("law pending(int x)"));
        CPPL_CHECK(draft->syntax.proofs[0].completeness == Completeness::AwaitingClause);
        CPPL_CHECK_EQ(draft->syntax.proofs[0].name, std::string("owed"));
    }
    // A declaration C++ completes stays C++, and a name with no parameters
    // yet is not even a head: `proof foo;` declares a variable.
    const auto cpp = recognized("law make(int);\nproof foo;\n");
    CPPL_CHECK(cpp->syntax.proofs.empty());
}

CPPL_TEST(a_draft_keeps_a_proof_without_its_body_and_one_whose_body_is_unterminated) {
    const auto bodiless = recognized("proof claim(int x)\n    proves (x == x)\n");
    CPPL_CHECK_EQ(bodiless->syntax.proofs.size(), std::size_t{1});
    if (!bodiless->syntax.proofs.empty()) {
        CPPL_CHECK(bodiless->syntax.proofs[0].completeness == Completeness::AwaitingBody);
        CPPL_CHECK_EQ(spelled(*bodiless, bodiless->syntax.proofs[0].proposition), std::string("x == x"));
    }

    const std::string open = "proof open(int x)\n    proves (x == x)\n{\n    assume h : x == x;\n    exact h;\n";
    CPPL_CHECK(recognized(open, RecognitionMode::Compile)->syntax.proofs.empty());
    const auto draft = recognized(open);
    CPPL_CHECK_EQ(draft->syntax.proofs.size(), std::size_t{1});
    if (!draft->syntax.proofs.empty()) {
        const ProofDeclaration& proof = draft->syntax.proofs[0];
        CPPL_CHECK(proof.completeness == Completeness::UnterminatedBody);
        CPPL_CHECK_EQ(proof.body.end(), draft->text.size());
        CPPL_CHECK_EQ(proof.statements.size(), std::size_t{2});
    }
}

CPPL_TEST(a_draft_reads_on_past_a_statement_it_cannot_read) {
    const std::string text = "proof on(int x)\n    proves (x == x)\n{\n"
                             "    ass\n    assume h : x == x;\n"
                             "    cases x {\n        State::one => { exact; refl; }\n    }\n"
                             "    exact h;\n}\n";
    // A compiled unit refuses the proof.
    CPPL_CHECK(recognized(text, RecognitionMode::Compile)->syntax.proofs.empty());
    const auto draft = recognized(text);
    CPPL_CHECK_EQ(draft->syntax.proofs.size(), std::size_t{1});
    if (draft->syntax.proofs.empty()) {
        return;
    }
    const std::vector<ProofStatement>& statements = draft->syntax.proofs[0].statements;
    CPPL_CHECK_EQ(statements.size(), std::size_t{3});
    if (statements.size() != 3) {
        return;
    }
    // `ass` runs to the `;` that ends what follows it: the draft cannot tell
    // where a statement without its `;` ends.
    CPPL_CHECK(statements[0].kind == ProofStatementKind::Unread);
    CPPL_CHECK_EQ(spelled(*draft, statements[0].span), std::string("ass\n    assume h : x == x;"));
    CPPL_CHECK(statements[1].kind == ProofStatementKind::Cases);
    CPPL_CHECK(statements[2].kind == ProofStatementKind::Exact);
    // An arm keeps what it could read.
    const std::vector<ProofStatement>& arm = statements[1].arms.at(0).statements;
    CPPL_CHECK_EQ(arm.size(), std::size_t{2});
    if (arm.size() == 2) {
        CPPL_CHECK(arm[0].kind == ProofStatementKind::Unread);
        CPPL_CHECK(arm[1].kind == ProofStatementKind::Reflexivity);
    }
    CPPL_CHECK_EQ(spelled(*draft, statements[2].span), std::string("exact h;"));
}

CPPL_TEST(a_statement_may_begin_at_the_start_of_a_body_and_after_each_statement) {
    const std::string head = "proof mine(int x)\n    proves (x == x)\n{\n";
    CPPL_CHECK(admissible(*recognized(head + "    |\n}\n")).statement);
    CPPL_CHECK(admissible(*recognized(head + "    refl;\n    |\n}\n")).statement);
    CPPL_CHECK(
        admissible(*recognized(head + "    cases x {\n        State::one => { refl; }\n    }\n    |\n}\n")).statement);
    // Inside an arm's body too, and while the body is still unterminated.
    CPPL_CHECK(admissible(*recognized(head + "    cases x {\n        State::one => { | }\n    }\n}\n")).statement);
    CPPL_CHECK(admissible(*recognized(head + "    refl;\n    |")).statement);
    // Not after a statement's first word, nor inside its proposition.
    const Admissible after_assume = admissible(*recognized(head + "    assume |\n}\n"));
    CPPL_CHECK(!after_assume.statement);
    CPPL_CHECK(!after_assume.evidence_for.has_value());
    CPPL_CHECK(!admissible(*recognized(head + "    assume h : x == |\n}\n")).statement);
    // Between a statement's arms nothing but an arm is written.
    CPPL_CHECK(!admissible(*recognized(head + "    cases x {\n        | \n    }\n}\n")).statement);
    // Outside any body there are no statements.
    CPPL_CHECK(!admissible(*recognized("int main() {\n    |\n}\n")).statement);
}

CPPL_TEST(a_statement_that_names_evidence_asks_for_it_after_its_word) {
    const std::string head = "proof mine(int x)\n    proves (x == x)\n{\n";
    for (const std::string_view word : {"exact", "apply", "rewrite", "contradiction"}) {
        const Admissible here = admissible(*recognized(head + "    " + std::string(word) + " |\n}\n"));
        CPPL_CHECK(here.evidence_for == statement_keyword(word));
        CPPL_CHECK(here.proof.has_value());
    }
    // An unreadable statement before it changes nothing.
    const Admissible past = admissible(*recognized(head + "    rewrite\n    refl;\n    exact |\n}\n"));
    CPPL_CHECK(past.evidence_for == ProofStatementKind::Exact);
}

CPPL_TEST(evidence_is_what_elaboration_would_look_for_in_scope) {
    const auto draft = recognized("trusted law axiom(int x)\n    proves (x == x);\n"
                                  "law proved(int x)\n    proves (x == x)\n{\n    refl;\n}\n"
                                  "proof other(int y)\n    proves (y == y)\n{\n    refl;\n}\n"
                                  "proof mine(int x)\n    proves (x == x)\n{\n"
                                  "    assume outer : x == x;\n"
                                  "    cases x {\n"
                                  "        State::one => { assume sibling : x == x; refl; }\n"
                                  "        State::two => { assume inner : x == x; exact | }\n"
                                  "    }\n"
                                  "}\n");
    const Admissible here = admissible(*draft);
    CPPL_CHECK(here.evidence_for == ProofStatementKind::Exact);
    if (!here.proof.has_value()) {
        return;
    }
    const std::vector<Evidence> evidence = evidence_at(draft->syntax, *here.proof, draft->cursor);
    // The innermost assumption first; a sibling arm's is out of scope; a
    // proof never names itself; a Law proved where it is declared is a proof.
    CPPL_CHECK_EQ(names(evidence), std::string("inner outer axiom proved other"));
    CPPL_CHECK(evidence.front().kind == Evidence::Kind::Assumption);
    CPPL_CHECK(evidence[2].kind == Evidence::Kind::TrustedLaw);
    CPPL_CHECK(evidence.back().kind == Evidence::Kind::Proof);
}

CPPL_TEST(clauses_are_admitted_after_parameters_and_clauses_in_order) {
    const Admissible law = admissible(*recognized("law l(int x) |\n"));
    CPPL_CHECK(law.owner == ClauseOwner::Law);
    CPPL_CHECK(admits(law, ClauseKind::Expects));
    CPPL_CHECK(admits(law, ClauseKind::Proves));
    // After `expects`, the conclusion; after the conclusion, nothing.
    const Admissible premised = admissible(*recognized("law l(int x)\n    expects (x > 0) |\n"));
    CPPL_CHECK(!admits(premised, ClauseKind::Expects));
    CPPL_CHECK(admits(premised, ClauseKind::Proves));
    CPPL_CHECK(admissible(*recognized("law l(int x)\n    proves (x == x) |;\n")).clauses.empty());
    // Before an existing conclusion, only its premise.
    const Admissible inserted = admissible(*recognized("law l(int x) |\n    proves (x == x);\n"));
    CPPL_CHECK(admits(inserted, ClauseKind::Expects));
    CPPL_CHECK(!admits(inserted, ClauseKind::Proves));

    const Admissible proof = admissible(*recognized("proof p(int x) |\n"));
    CPPL_CHECK(proof.owner == ClauseOwner::Proof);
    CPPL_CHECK(admits(proof, ClauseKind::Proves));
    CPPL_CHECK(!admits(proof, ClauseKind::Expects));

    const Admissible contract =
        admissible(*recognized("verified int keep(int x)\n    expects (x > 0)\n    |\n{\n    return x;\n}\n"));
    CPPL_CHECK(contract.owner == ClauseOwner::VerifiedFunction);
    CPPL_CHECK(admits(contract, ClauseKind::Ensures));
    CPPL_CHECK(!admits(contract, ClauseKind::Expects));
    CPPL_CHECK(!admits(contract, ClauseKind::Decreases));
    // Inside the body, none.
    CPPL_CHECK(admissible(*recognized("verified int keep(int x)\n{\n    return x; |\n}\n")).clauses.empty());
}

CPPL_TEST(a_declaration_may_begin_where_the_recognizer_would_read_one) {
    CPPL_CHECK(admissible(*recognized("|")).declaration);
    CPPL_CHECK(admissible(*recognized("int helper();\n|\n")).declaration);
    CPPL_CHECK(admissible(*recognized("#include <vector>\n|\n")).declaration);
    CPPL_CHECK(admissible(*recognized("namespace n {\n|\n}\n")).declaration);
    CPPL_CHECK(!admissible(*recognized("int value = |;\n")).declaration);
    CPPL_CHECK(!admissible(*recognized("law l(int x)\n    proves (x == |);\n")).declaration);
    CPPL_CHECK(!admissible(*recognized("proof p(int x)\n    proves (x == x)\n{\n    |\n}\n")).declaration);
}
