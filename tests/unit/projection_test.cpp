// Projection and erasure properties.
//
// The runtime program must be the scanned program with C++L-only spans removed
// and nothing else changed. That property is what makes a C++17 target stay
// C++17: erasure can only delete, so it cannot introduce a construct from a
// later standard (COMPATIBILITY.md, TRUST.md 29).

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/erasure/erase.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/projection.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

const std::string kUnit = "# 1 \"main.cpp\"\n"
                          "pure int identity(int x) {\n"
                          "    return x;\n"
                          "}\n"
                          "law identity_returns_input(int x)\n"
                          "    proves (identity(x) == x);\n"
                          "proof identity_returns_input_holds(int x)\n"
                          "    proves (identity_returns_input(x))\n"
                          "{\n"
                          "    refl;\n"
                          "}\n"
                          "law identity_of_zero()\n"
                          "    proves (identity(0) == 0);\n"
                          "proof identity_of_zero_holds()\n"
                          "    proves (identity_of_zero())\n"
                          "{\n"
                          "    exact identity_returns_input_holds(0);\n"
                          "}\n"
                          "law identity_under_a_premise(int x)\n"
                          "    expects (identity(x) == 0)\n"
                          "    proves (identity(x) == x);\n"
                          "proof identity_under_a_premise_holds(int x)\n"
                          "    proves (identity_under_a_premise(x))\n"
                          "{\n"
                          "    assume h : identity(x) == 0;\n"
                          "    refl;\n"
                          "}\n"
                          "int main() { return identity(0); }\n";

std::size_t count_newlines(std::string_view text) {
    return static_cast<std::size_t>(std::ranges::count(text, '\n'));
}

// The source map editors read the analysis text through: every run it says was
// kept or copied spells, where it says it now is, exactly what was written, and
// what was kept is in order and never overlaps what was generated.
bool maps_exactly(const std::string& text, const std::string& name) {
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(text, name);
    const auto syntax = cppl::frontend::recognize(stream, engine);
    const auto projection = cppl::frontend::project(stream, syntax, {});
    std::size_t kept_to = 0;
    std::size_t written_to = 0;
    for (const auto& segment : projection.segments) {
        if (segment.analysis < kept_to || segment.original < written_to || segment.length == 0 ||
            projection.analysis.compare(segment.analysis, segment.length, text, segment.original, segment.length) !=
                0) {
            return false;
        }
        kept_to = segment.analysis + segment.length;
        written_to = segment.original + segment.length;
    }
    for (const auto& copy : projection.copies) {
        if (copy.original.length == 0 || projection.analysis.compare(copy.analysis, copy.original.length, text,
                                                                     copy.original.offset, copy.original.length) != 0) {
            return false;
        }
        for (const auto& segment : projection.segments) {
            if (copy.analysis < segment.analysis + segment.length &&
                segment.analysis < copy.analysis + copy.original.length) {
                return false;
            }
        }
    }
    return true;
}

std::string read_fixture(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace

CPPL_TEST(physical_declarations_stay_distinct_when_displayed_locations_repeat) {
    const std::string source = "#line 1 \"same.cpp\"\n"
                               "verified unsigned a() ensures (result == 0u) {return 0u;}"
                               "law same() proves (0u == 0u);\n"
                               "#line 1 \"same.cpp\"\n"
                               "verified unsigned b() ensures (result == 1u) {return 1u;}"
                               "namespace B {law same() proves (1u == 1u);}\n"
                               "#line 5 \"same.cpp\"\n"
                               "pure unsigned c() {return 2u;}\n"
                               "#line 5 \"same.cpp\"\n"
                               "pure unsigned d() {return 3u;}";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(source, "input.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    const auto projection = cppl::frontend::project(stream, syntax, {});
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(projection.declaration_offsets.size(), 4u);
    CPPL_CHECK_EQ(projection.specification_functions.size(), 2u);
    CPPL_CHECK(projection.specification_functions[0].analysis_offset !=
               projection.specification_functions[1].analysis_offset);
    for (const auto& declaration : projection.declaration_offsets) {
        CPPL_CHECK_EQ(source[declaration.original], projection.analysis[declaration.analysis]);
        CPPL_CHECK_EQ(projection.declaration_offset(declaration.original), declaration.analysis);
    }
    for (const auto& law : projection.specification_functions) {
        CPPL_CHECK_EQ(projection.analysis.substr(law.analysis_offset, law.name.size()), law.name);
    }
    CPPL_CHECK(!projection.declaration_offset(source.size()).has_value());
    const auto erased = cppl::erasure::erase(stream, syntax, projection, engine);
    CPPL_CHECK(erased.report.only_deletions);
    CPPL_CHECK(erased.report.lines_preserved);
    CPPL_CHECK(!engine.has_errors());
}

CPPL_TEST(the_runtime_program_only_loses_text) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(projection.runtime.size(), kUnit.size());
    CPPL_CHECK_EQ(count_newlines(projection.runtime), count_newlines(kUnit));

    for (std::size_t offset = 0; offset < kUnit.size(); ++offset) {
        if (projection.runtime[offset] != kUnit[offset]) {
            CPPL_CHECK_EQ(projection.runtime[offset], ' ');
        }
    }
}

CPPL_TEST(every_runtime_token_is_an_original_token_in_its_original_place) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    const cppl::frontend::TokenStream runtime = cppl::frontend::lex(projection.runtime, "main.cpp");

    std::size_t original_index = 0;
    for (const cppl::frontend::Token& token : runtime.tokens()) {
        if (token.kind == cppl::frontend::TokenKind::EndOfFile) {
            continue;
        }
        bool matched = false;
        while (original_index < stream.tokens().size()) {
            const cppl::frontend::Token& candidate = stream.tokens()[original_index++];
            if (candidate.span.offset == token.span.offset && candidate.text == token.text &&
                candidate.line == token.line && candidate.column == token.column) {
                matched = true;
                break;
            }
        }
        CPPL_CHECK(matched);
    }
}

CPPL_TEST(the_runtime_program_carries_no_formal_syntax) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    const cppl::frontend::TokenStream runtime = cppl::frontend::lex(projection.runtime, "main.cpp");
    for (const cppl::frontend::Token& token : runtime.tokens()) {
        CPPL_CHECK(token.text != "law");
        CPPL_CHECK(token.text != "ensures");
        CPPL_CHECK(token.text != "pure");
        CPPL_CHECK(token.text != "proof");
        CPPL_CHECK(token.text != "proves");
        CPPL_CHECK(token.text != "refl");
        CPPL_CHECK(token.text != "exact");
        CPPL_CHECK(token.text != "expects");
        CPPL_CHECK(token.text != "assume");
        CPPL_CHECK(token.text != "rewrite");
    }
}

CPPL_TEST(the_analysis_program_carries_the_proposition_for_clang_to_resolve) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK_EQ(projection.specification_functions.size(), std::size_t{3});
    // A law is projected under its own name, so a proof can name it through
    // ordinary C++ lookup (GRAMMAR.md 46).
    CPPL_CHECK_EQ(projection.specification_functions[0].name, std::string("identity_returns_input"));
    CPPL_CHECK(projection.analysis.find("bool identity_returns_input(int x)") != std::string::npos);
    CPPL_CHECK(projection.analysis.find("identity(x) == x") != std::string::npos);
    CPPL_CHECK(projection.analysis.find("#line") != std::string::npos);
}

CPPL_TEST(the_analysis_program_carries_the_proposition_a_proof_claims) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK_EQ(projection.proof_functions.size(), std::size_t{3});
    const std::string& name = projection.proof_functions[0].name;
    CPPL_CHECK(projection.analysis.find(name) != std::string::npos);
    CPPL_CHECK(projection.analysis.find("identity_returns_input(x)") != std::string::npos);

    // The proof statements are C++L and are never projected into C++.
    CPPL_CHECK(projection.analysis.find("refl") == std::string::npos);
    CPPL_CHECK(projection.analysis.find("exact") == std::string::npos);
}

CPPL_TEST(the_analysis_program_carries_each_term_a_proof_instantiates_at) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK(projection.proof_functions[0].argument_names.empty());
    CPPL_CHECK_EQ(projection.proof_functions[1].argument_names.size(), std::size_t{1});

    // The term is emitted as an ordinary C++ expression for Clang to resolve,
    // with the type it is given deduced from the expression itself.
    const std::string& argument = projection.proof_functions[1].argument_names[0];
    const std::size_t emitted = projection.analysis.find("static decltype(auto) " + argument + "()");
    CPPL_CHECK(emitted != std::string::npos);
    CPPL_CHECK(projection.analysis.find("return (", emitted) != std::string::npos);
}

CPPL_TEST(the_analysis_program_carries_the_premise_a_law_supposes) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    // Only the law that states one has a premise function, and it is generated
    // rather than named after the law: the law's own name states what it
    // concludes.
    CPPL_CHECK(projection.specification_functions[0].premise_name.empty());
    CPPL_CHECK(projection.specification_functions[1].premise_name.empty());

    const std::string& premise = projection.specification_functions[2].premise_name;
    CPPL_CHECK(!premise.empty());
    CPPL_CHECK(premise != projection.specification_functions[2].name);
    CPPL_CHECK(projection.analysis.find("bool " + premise + "(int x)") != std::string::npos);
}

CPPL_TEST(the_analysis_program_carries_the_proposition_a_statement_assumes) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK(projection.proof_functions[0].assumption_names.empty());
    CPPL_CHECK_EQ(projection.proof_functions[2].assumption_names.size(), std::size_t{1});

    const std::string& assumed = projection.proof_functions[2].assumption_names[0];
    CPPL_CHECK(projection.analysis.find("static decltype(auto) " + assumed + "(int x)") != std::string::npos);

    // The statement itself stays C++L: what reaches Clang is the proposition
    // the statement names, never the statement.
    const cppl::frontend::TokenStream analysis = cppl::frontend::lex(projection.analysis, "main.cpp");
    for (const cppl::frontend::Token& token : analysis.tokens()) {
        CPPL_CHECK(token.text != "assume");
    }
}

CPPL_TEST(formal_connectives_are_recorded_with_specification_precedence) {
    // Equivalence is looser than implication, which is looser than `&&`, and a
    // chain of equivalences groups to the left (GRAMMAR.md 30, 33). Classical
    // logic cannot tell the two groupings of a chain apart, so the recorded
    // shape is what pins which one the projector built.
    const std::string source =
        "# 1 \"main.cpp\"\n"
        "law loosest(unsigned x)\n"
        "    proves (Eq<unsigned>(x, x) && Eq<unsigned>(x, x) -> Eq<unsigned>(x, x) <-> Eq<unsigned>(x, x));\n"
        "law chained(unsigned x)\n"
        "    proves (Eq<unsigned>(x, x) <-> Eq<unsigned>(x, x) <-> Eq<unsigned>(x, x));\n"
        "law between(unsigned x)\n"
        "    proves (Eq<unsigned>(x, x) && Eq<unsigned>(x, x) || Eq<unsigned>(x, x) -> Eq<unsigned>(x, x));\n"
        "int main() { return 0; }\n";

    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(source, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK(projection.diagnostics.empty());
    CPPL_CHECK_EQ(projection.proposition_probes.size(), std::size_t{3});

    using Kind = cppl::source::ProjectionKind;
    const cppl::source::ProjectionShape& loosest = projection.proposition_probes[0].shape;
    CPPL_CHECK(loosest.kind == Kind::Equivalence);
    CPPL_CHECK_EQ(loosest.children.size(), std::size_t{2});
    CPPL_CHECK(loosest.children[0].kind == Kind::Implication);
    CPPL_CHECK(loosest.children[0].children[0].kind == Kind::Conjunction);
    CPPL_CHECK(loosest.children[0].children[1].kind == Kind::Equality);
    CPPL_CHECK(loosest.children[1].kind == Kind::Equality);

    const cppl::source::ProjectionShape& chained = projection.proposition_probes[1].shape;
    CPPL_CHECK(chained.kind == Kind::Equivalence);
    CPPL_CHECK(chained.children[0].kind == Kind::Equivalence);
    CPPL_CHECK(chained.children[1].kind == Kind::Equality);

    // `||` stands between them: looser than `&&`, tighter than `->`.
    const cppl::source::ProjectionShape& between = projection.proposition_probes[2].shape;
    CPPL_CHECK(between.kind == Kind::Implication);
    CPPL_CHECK(between.children[0].kind == Kind::Disjunction);
    CPPL_CHECK(between.children[0].children[0].kind == Kind::Conjunction);
    CPPL_CHECK(between.children[0].children[1].kind == Kind::Equality);
    CPPL_CHECK(between.children[1].kind == Kind::Equality);
}

CPPL_TEST(erasure_reports_the_properties_it_checked) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    const cppl::erasure::Erased erased = cppl::erasure::erase(stream, syntax, projection, engine);

    CPPL_CHECK(erased.report.only_deletions);
    CPPL_CHECK(erased.report.lines_preserved);
    CPPL_CHECK(erased.report.erased_spans == 7);
    CPPL_CHECK(erased.report.erased_bytes > 0);
    CPPL_CHECK(!engine.has_errors());
}

CPPL_TEST(a_unit_without_formal_syntax_is_left_untouched) {
    const std::string ordinary = "int main() { return 0; }\n";

    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(ordinary, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);

    CPPL_CHECK(syntax.empty());

    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});
    CPPL_CHECK_EQ(projection.runtime, ordinary);
    CPPL_CHECK_EQ(projection.analysis, ordinary);
}

CPPL_TEST(contracts_erase_without_changing_runtime_values_or_source_locations) {
    const std::string text = "verified unsigned f(unsigned x)\n"
                             "expects (x == 0u)\nensures (result == 0u)\n{ return x; } "
                             "verified unsigned g(unsigned y) ensures (result == y) { return y; }\n"
                             "int result = 7;\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(text, "contracts.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    const auto projection = cppl::frontend::project(stream, syntax, {});
    const auto erased = cppl::erasure::erase(stream, syntax, projection, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(erased.report.only_deletions);
    CPPL_CHECK(erased.report.lines_preserved);
    CPPL_CHECK_EQ(projection.contract_functions.size(), std::size_t{2});
    CPPL_CHECK(projection.runtime.find("int result = 7;") != std::string::npos);
    CPPL_CHECK(projection.runtime.find("return x;") != std::string::npos);
    CPPL_CHECK(projection.runtime.find("return y;") != std::string::npos);

    const auto analysis = cppl::frontend::lex(projection.analysis, "contracts.cpp");
    for (const auto& function : syntax.verified_functions) {
        bool found = false;
        for (const auto& token : analysis.tokens()) {
            if (token.text == function.function_name) {
                const auto location = analysis.location_of(token);
                CPPL_CHECK_EQ(location.line, function.function_location.line);
                CPPL_CHECK_EQ(location.column, function.function_location.column);
                found = true;
            }
        }
        CPPL_CHECK(found);
    }
}

CPPL_TEST(loop_invariants_leave_the_runtime_and_reach_clang_inside_the_body) {
    // One 'invariant' clause states both conjuncts (SPEC.md 11.5,
    // CONTRACT-003); the loop rule splits the conjunction into its own
    // entry and preservation obligations further down the pipeline.
    const std::string text = "verified unsigned f(unsigned n) ensures (result == n) {\n"
                             "    unsigned i = 0u;\n"
                             "    while (i < n)\n"
                             "        invariant (i <= n && i >= 0u) { i = i + 1u; }\n"
                             "    return i;\n"
                             "}\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(text, "loops.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(syntax.loops.size(), std::size_t{1});
    CPPL_CHECK_EQ(syntax.loops[0].invariants.size(), std::size_t{1});
    const auto projection = cppl::frontend::project(stream, syntax, {});
    const auto erased = cppl::erasure::erase(stream, syntax, projection, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(erased.report.only_deletions);
    CPPL_CHECK(erased.report.lines_preserved);
    CPPL_CHECK(projection.runtime.find("invariant") == std::string::npos);
    CPPL_CHECK(projection.runtime.find("while (i < n)") != std::string::npos);
    CPPL_CHECK(projection.runtime.find("{ i = i + 1u; }") != std::string::npos);
    CPPL_CHECK_EQ(projection.loop_invariants.size(), std::size_t{1});
    for (const auto& marker : projection.loop_invariants) {
        CPPL_CHECK(projection.analysis.find("bool " + marker.name + " = (") != std::string::npos);
    }
    // The body's own statements keep their line and column in the analysis.
    const auto analysis = cppl::frontend::lex(projection.analysis, "loops.cpp");
    bool found = false;
    for (const auto& token : analysis.tokens()) {
        if (token.text == "i" && analysis.location_of(token).line == 4 && analysis.location_of(token).column == 41) {
            found = true;
        }
    }
    CPPL_CHECK(found);
}

CPPL_TEST(a_loop_measure_leaves_the_runtime_and_reaches_clang_as_a_value) {
    // A `decreases` measure resolves in the loop head's scope like an
    // invariant, but it is an integer rather than a condition, so it is
    // declared `auto` and keeps the type the expression already has.
    const std::string text = "verified unsigned f(unsigned n) ensures (result == n) {\n"
                             "    unsigned i = 0u;\n"
                             "    while (i < n)\n"
                             "        invariant (i <= n)\n"
                             "        decreases (n - i) { i = i + 1u; }\n"
                             "    return i;\n"
                             "}\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(text, "loops.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(syntax.loops.size(), std::size_t{1});
    CPPL_CHECK(syntax.loops[0].decreases.has_value());

    const auto projection = cppl::frontend::project(stream, syntax, {});
    const auto erased = cppl::erasure::erase(stream, syntax, projection, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(erased.report.only_deletions);

    // The clause is proof-only: nothing of it survives into the program.
    CPPL_CHECK(projection.runtime.find("decreases") == std::string::npos);
    CPPL_CHECK(projection.runtime.find("invariant") == std::string::npos);
    CPPL_CHECK(projection.runtime.find("while (i < n)") != std::string::npos);

    // One invariant marker and one measure marker reach Clang.
    CPPL_CHECK_EQ(projection.loop_invariants.size(), std::size_t{2});
    std::size_t measures = 0;
    for (const auto& marker : projection.loop_invariants) {
        if (marker.measure) {
            ++measures;
            CPPL_CHECK(projection.analysis.find("auto " + marker.name + " = (") != std::string::npos);
        } else {
            CPPL_CHECK(projection.analysis.find("bool " + marker.name + " = (") != std::string::npos);
        }
    }
    CPPL_CHECK_EQ(measures, std::size_t{1});
}

CPPL_TEST(a_clause_expression_keeps_its_written_position_in_the_analysis) {
    // A diagnostic inside a clause points where the author wrote it, however
    // long the declaration generated around the expression is.
    const std::string text = "verified unsigned f(unsigned first_parameter, unsigned second_parameter)\n"
                             "    ensures (result == first_parameter)\n"
                             "{\n"
                             "    return first_parameter;\n"
                             "}\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(text, "clauses.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    CPPL_CHECK(!engine.has_errors());
    const auto projection = cppl::frontend::project(stream, syntax, {});
    const auto analysis = cppl::frontend::lex(projection.analysis, "clauses.cpp");
    bool result_found = false;
    bool parameter_found = false;
    for (const auto& token : analysis.tokens()) {
        const auto at = analysis.location_of(token);
        result_found = result_found || (token.text == "result" && at.line == 2 && at.column == 14);
        parameter_found = parameter_found || (token.text == "first_parameter" && at.line == 2 && at.column == 24);
    }
    CPPL_CHECK(result_found);
    CPPL_CHECK(parameter_found);
}

namespace {

// One construct of every erased kind, and a refinement last so that the runtime
// program shares its byte offsets with this text up to the lowering.
const std::string kErasedKinds = "# 1 \"erased.cpp\"\n"
                                 "pure unsigned id(unsigned x) { return x; }\n"
                                 "law id_is_identity(unsigned x)\n"
                                 "    proves (id(x) == x);\n"
                                 "trusted law assumed(unsigned x)\n"
                                 "    proves (x == x);\n"
                                 "proof pinned(unsigned v)\n"
                                 "    proves (v == v)\n"
                                 "{\n"
                                 "    refl;\n"
                                 "}\n"
                                 "verified unsigned f(unsigned x)\n"
                                 "    expects (x < 5u)\n"
                                 "    ensures (result == x)\n"
                                 "{\n"
                                 "    unsigned i = 0u;\n"
                                 "    while (i < x)\n"
                                 "        invariant (i <= x)\n"
                                 "        decreases (x - i)\n"
                                 "    {\n"
                                 "        i = i + 1u;\n"
                                 "    }\n"
                                 "    if (x >= 5u)\n"
                                 "        contradiction pinned(x + 1u);\n"
                                 "    return i;\n"
                                 "}\n"
                                 "type Small = unsigned\n"
                                 "    where (self < 10u);\n";

struct ErasedUnit {
    cppl::frontend::TokenStream stream;
    cppl::frontend::Syntax syntax;
    cppl::frontend::Projection projection;
};

ErasedUnit erased_unit() {
    cppl::diagnostics::Engine engine;
    cppl::frontend::TokenStream stream = cppl::frontend::lex(kErasedKinds, "erased.cpp");
    cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    CPPL_CHECK(!engine.has_errors());
    cppl::frontend::Projection projection = cppl::frontend::project(stream, syntax, {});
    return ErasedUnit{std::move(stream), std::move(syntax), std::move(projection)};
}

// Every span the syntax says the runtime program must not contain.
std::vector<cppl::source::ByteSpan> proof_only_spans(const cppl::frontend::Syntax& syntax) {
    std::vector<cppl::source::ByteSpan> spans;
    spans.reserve(syntax.laws.size() + syntax.proofs.size() + syntax.pure_markers.size() +
                  2 * syntax.verified_functions.size() + syntax.loops.size() + syntax.path_contradictions.size());
    for (const auto& law : syntax.laws)
        spans.push_back(law.range.span);
    for (const auto& proof : syntax.proofs)
        spans.push_back(proof.range.span);
    for (const auto& marker : syntax.pure_markers)
        spans.push_back(marker.keyword);
    for (const auto& verified : syntax.verified_functions) {
        spans.push_back(verified.keyword);
        spans.push_back(verified.clause_region);
    }
    for (const auto& loop : syntax.loops)
        spans.push_back(loop.clause_region);
    for (const auto& claim : syntax.path_contradictions)
        spans.push_back(claim.erased);
    return spans;
}

// Checks a runtime program erasure must refuse, and returns what it reported.
cppl::erasure::Report refused(const ErasedUnit& unit, std::string runtime) {
    cppl::frontend::Projection projection = unit.projection;
    projection.runtime = std::move(runtime);
    cppl::diagnostics::Engine engine;
    const auto erased = cppl::erasure::erase(unit.stream, unit.syntax, projection, engine);
    CPPL_CHECK(!erased.report.preserved());
    CPPL_CHECK(engine.has_errors());
    return erased.report;
}

} // namespace

// SPEC: ERASE-005, ERASE-007, ERASEMATRIX-001
// TRUST.md TCB-ERASE-001, TCB-ERASE-006; ARCHITECTURE.md ARCH-ERASE-002
CPPL_TEST(the_projector_erases_every_kind_of_proof_only_span) {
    const ErasedUnit unit = erased_unit();
    CPPL_CHECK_EQ(unit.syntax.laws.size(), std::size_t{2});
    CPPL_CHECK_EQ(unit.syntax.proofs.size(), std::size_t{1});
    CPPL_CHECK_EQ(unit.syntax.pure_markers.size(), std::size_t{1});
    CPPL_CHECK_EQ(unit.syntax.verified_functions.size(), std::size_t{1});
    CPPL_CHECK_EQ(unit.syntax.loops.size(), std::size_t{1});
    CPPL_CHECK_EQ(unit.syntax.path_contradictions.size(), std::size_t{1});
    CPPL_CHECK_EQ(unit.syntax.refinement_types.size(), std::size_t{1});

    cppl::diagnostics::Engine engine;
    const auto erased = cppl::erasure::erase(unit.stream, unit.syntax, unit.projection, engine);
    CPPL_CHECK(erased.report.preserved());
    CPPL_CHECK(erased.report.spans_erased);
    CPPL_CHECK_EQ(erased.report.erased_spans, proof_only_spans(unit.syntax).size());
    CPPL_CHECK_EQ(erased.report.lowered_spans, std::size_t{1});
    CPPL_CHECK(!engine.has_errors());
}

// Erasure checks that each proof-only span is gone, not only that whatever
// changed lies inside one. A span the projector left in place would otherwise
// reach Clang as runtime code.
CPPL_TEST(a_proof_only_span_left_in_the_runtime_program_is_refused) {
    const ErasedUnit unit = erased_unit();
    const std::vector<cppl::source::ByteSpan> spans = proof_only_spans(unit.syntax);
    CPPL_CHECK_EQ(spans.size(), std::size_t{8});
    for (const auto& span : spans) {
        std::string runtime = unit.projection.runtime;
        runtime.replace(span.offset, span.length, kErasedKinds, span.offset, span.length);
        const auto report = refused(unit, std::move(runtime));
        CPPL_CHECK(!report.spans_erased);
        CPPL_CHECK(report.only_deletions);
    }

    // One byte of a span surviving is enough.
    const auto& keyword = unit.syntax.verified_functions[0].keyword;
    std::string runtime = unit.projection.runtime;
    runtime[keyword.offset] = kErasedKinds[keyword.offset];
    CPPL_CHECK(!refused(unit, std::move(runtime)).spans_erased);
}

CPPL_TEST(a_runtime_program_changed_beyond_erasure_is_refused) {
    const ErasedUnit unit = erased_unit();

    // Inside an erased span, anything but a blank is an addition.
    const auto& clauses = unit.syntax.verified_functions[0].clause_region;
    std::string altered = unit.projection.runtime;
    altered[clauses.offset + 1] = 'X';
    CPPL_CHECK(!refused(unit, std::move(altered)).only_deletions);

    // Outside one, no byte may change at all.
    std::string rewritten = unit.projection.runtime;
    const std::size_t returned = rewritten.find("return i;");
    CPPL_CHECK(returned != std::string::npos);
    rewritten[returned + 7] = '0';
    CPPL_CHECK(!refused(unit, std::move(rewritten)).only_deletions);

    // A claim keeps its `;`: erasing it too would make the `return` the body of
    // the unbraced `if` above it (ERASE-016).
    const auto& claim = unit.syntax.path_contradictions[0];
    std::string unterminated = unit.projection.runtime;
    CPPL_CHECK_EQ(unterminated[claim.span.end() - 1], ';');
    unterminated[claim.span.end() - 1] = ' ';
    CPPL_CHECK(!refused(unit, std::move(unterminated)).only_deletions);

    // A newline blanked away moves every line below it.
    std::string joined = unit.projection.runtime;
    joined[clauses.end() - 1] = ' ';
    CPPL_CHECK(!refused(unit, std::move(joined)).preserved());
}

// SPEC: ERASE-004, ERASE-010, ABI-002
// TRUST.md TCB-ERASE-002, TCB-ERASE-007
CPPL_TEST(a_refinement_lowered_to_anything_but_its_base_alias_is_refused) {
    const ErasedUnit unit = erased_unit();
    const std::string canonical = "using Small = unsigned;";
    CPPL_CHECK(unit.projection.runtime.find(canonical + "\n") != std::string::npos);

    // A different representation, a wrapper, or the predicate kept as a check:
    // each changes what the program computes or how it is laid out.
    for (const std::string& lowering :
         {std::string("using Small = unsigned long;"), std::string("struct Small { unsigned value; };"),
          std::string("using Small = unsigned; static_assert(true);")}) {
        std::string runtime = unit.projection.runtime;
        runtime.replace(runtime.find(canonical), canonical.size(), lowering);
        CPPL_CHECK(!refused(unit, std::move(runtime)).lowerings_canonical);
    }
}

// SPEC: CASE-017, ERASE-016
// A split erases to an empty statement where its closing brace was, and Clang is
// given a block in its place: the split's marker and subject, each expression
// label, and per arm its marker, its binders under their written names and its
// claims.
CPPL_TEST(a_split_on_a_path_leaves_an_empty_statement_behind) {
    const std::string text = "proof pinned(unsigned v) proves (v == v) { refl; }\n"
                             "verified unsigned f(E s, bool flag) ensures (result == 0u) {\n"
                             "    if (flag)\n"
                             "        cases s { E::a => {} unnamed(value) => { contradiction pinned(value); } }\n"
                             "    return 0u;\n"
                             "}\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(text, "splits.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(syntax.path_splits.size(), std::size_t{1});
    const auto projection = cppl::frontend::project(stream, syntax, {});
    const auto erased = cppl::erasure::erase(stream, syntax, projection, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(erased.report.only_deletions);
    CPPL_CHECK(erased.report.lowerings_canonical);
    CPPL_CHECK(erased.report.lines_preserved);

    const auto& split = syntax.path_splits[0];
    CPPL_CHECK_EQ(projection.runtime[split.span.end() - 1], ';');
    CPPL_CHECK(projection.runtime.find("cases") == std::string::npos);
    CPPL_CHECK(projection.runtime.find("contradiction") == std::string::npos);
    CPPL_CHECK(projection.runtime.find("    return 0u;") != std::string::npos);

    CPPL_CHECK_EQ(projection.path_splits.size(), std::size_t{1});
    const std::string& name = projection.path_splits[0].name;
    CPPL_CHECK(projection.analysis.find("bool " + name + " = true;") != std::string::npos);
    CPPL_CHECK(projection.analysis.find("decltype(auto) " + name + "_subject = (") != std::string::npos);
    CPPL_CHECK(projection.analysis.find("decltype(auto) " + name + "_label_0 = (") != std::string::npos);
    CPPL_CHECK(projection.analysis.find(name + "_label_1") == std::string::npos);
    CPPL_CHECK(projection.analysis.find("bool " + name + "_arm_1 = true;") != std::string::npos);
    CPPL_CHECK(projection.analysis.find("& value = ") != std::string::npos);
    // The claim in the arm is the split's, projected inside the arm's block.
    CPPL_CHECK_EQ(projection.path_contradictions.size(), std::size_t{1});
    const std::string& claim = projection.path_contradictions[0].name;
    CPPL_CHECK(projection.analysis.find("bool " + claim + " = true;") > projection.analysis.find(name + "_arm_1"));
    CPPL_CHECK_EQ(projection.binding_probes.size(), std::size_t{1});
    CPPL_CHECK_EQ(projection.binding_probes[0].subject, name);
    CPPL_CHECK_EQ(projection.binding_probes[0].label, "unnamed");

    // The text after the split keeps its line and column in the analysis.
    const auto analysis = cppl::frontend::lex(projection.analysis, "splits.cpp");
    bool found = false;
    for (const auto& token : analysis.tokens()) {
        if (token.text == "return" && analysis.location_of(token).line == 5 &&
            analysis.location_of(token).column == 5) {
            found = true;
        }
    }
    CPPL_CHECK(found);
}

// SPEC: VERIFIED-045, ERASE-016
// TRUST.md TCB-ERASE-010
CPPL_TEST(a_claim_that_a_path_cannot_occur_leaves_an_empty_statement_behind) {
    // An unbraced `if` whose body is the claim: if the `;` went with the words,
    // the `return` after it would become the `if`'s body.
    const std::string text = "proof pinned(unsigned v) proves (v == v) { refl; }\n"
                             "verified unsigned f(unsigned x) expects (x < 5u) ensures (result == x) {\n"
                             "    if (x >= 5u) contradiction pinned(x + 1u);\n"
                             "    return x;\n"
                             "}\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(text, "claims.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(syntax.path_contradictions.size(), std::size_t{1});
    const auto projection = cppl::frontend::project(stream, syntax, {});
    const auto erased = cppl::erasure::erase(stream, syntax, projection, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(erased.report.only_deletions);
    CPPL_CHECK(erased.report.lines_preserved);

    // The runtime keeps the `;` in the byte it stood at, and nothing else of
    // the claim.
    const auto& claim = syntax.path_contradictions[0];
    CPPL_CHECK_EQ(projection.runtime[claim.span.end() - 1], ';');
    CPPL_CHECK(projection.runtime.find("contradiction") == std::string::npos);
    CPPL_CHECK(projection.runtime.find("pinned(x") == std::string::npos);
    CPPL_CHECK(projection.runtime.find("    return x;") != std::string::npos);

    // Clang is given a block where the claim stood: the claim's marker, then one
    // declaration per argument, resolved in the scope the statement sees.
    CPPL_CHECK_EQ(projection.path_contradictions.size(), std::size_t{1});
    const std::string& name = projection.path_contradictions[0].name;
    CPPL_CHECK(projection.analysis.find("bool " + name + " = true;") != std::string::npos);
    CPPL_CHECK(projection.analysis.find("decltype(auto) " + name + "_argument_0 = (") != std::string::npos);
    CPPL_CHECK(projection.analysis.find("x + 1u\n);") != std::string::npos);

    // The text after the claim keeps its line and column in the analysis.
    const auto analysis = cppl::frontend::lex(projection.analysis, "claims.cpp");
    bool found = false;
    for (const auto& token : analysis.tokens()) {
        if (token.text == "return" && analysis.location_of(token).line == 4 &&
            analysis.location_of(token).column == 5) {
            found = true;
        }
    }
    CPPL_CHECK(found);
}

CPPL_TEST(every_kept_and_copied_run_spells_what_was_written) {
    CPPL_CHECK(maps_exactly(kUnit, "main.cpp"));
    CPPL_CHECK(maps_exactly("type Index(unsigned n) = unsigned where (self < n);\n"
                            "verified unsigned clamp(unsigned x, unsigned limit)\n"
                            "    expects (limit > 0u)\n"
                            "    ensures (result < limit)\n"
                            "{\n"
                            "    unsigned y = x;\n"
                            "    while (y >= limit)\n"
                            "        invariant (y >= 0u)\n"
                            "        decreases (y)\n"
                            "    {\n"
                            "        y = y - limit;\n"
                            "    }\n"
                            "    return y;\n"
                            "}\n",
                            "clamp.cpp"));
    // Every fixture, read as written, as an editor reads it.
    std::size_t read = 0;
    for (const auto& entry : std::filesystem::directory_iterator(CPPL_TEST_FIXTURES_DIR)) {
        if (entry.path().extension() == ".cpp") {
            CPPL_CHECK(maps_exactly(read_fixture(entry.path()), entry.path().filename().string()));
            ++read;
        }
    }
    CPPL_CHECK(read > 20);
}

CPPL_TEST(the_parameters_and_expressions_of_a_formal_declaration_are_copies) {
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(kUnit, "main.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    const auto projection = cppl::frontend::project(stream, syntax, {});
    const auto copies_of = [&projection](const cppl::source::ByteSpan& span) {
        return std::ranges::count_if(projection.copies, [&span](const auto& copy) { return copy.original == span; });
    };
    const auto& law = syntax.laws[2];
    CPPL_CHECK_EQ(law.name, std::string("identity_under_a_premise"));
    // The parameters stand in the declaration for the Law and in its premise's,
    // first in the Law's own, which is the one an editor maps a parameter to.
    CPPL_CHECK_EQ(copies_of(law.parameters), 2);
    const auto first =
        std::ranges::find_if(projection.copies, [&law](const auto& copy) { return copy.original == law.parameters; });
    CPPL_CHECK(first->analysis > projection.specification_functions[2].analysis_offset);
    CPPL_CHECK(first->analysis < projection.specification_functions[2].analysis_offset + law.name.size() + 2);
    // A proof's parameters stand in its own declaration and in every probe
    // it has: here, one assumption.
    const auto& proof = syntax.proofs[2];
    CPPL_CHECK_EQ(copies_of(proof.parameters), 2);
    // A proof that takes no parameters has nothing copied for them.
    CPPL_CHECK_EQ(copies_of(syntax.proofs[1].parameters), 0);
}
