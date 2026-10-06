// Projection tests (unit_projection_test): every kind of proof-only span
// the projector erases, and the runtime text left without them.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/erasure/erase.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "cppl/testing/test.hpp"
#include "projection_test_support.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace projection_test_detail;

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

// SPEC: ERASE-005, ERASE-017
// TRUST.md TCB-ERASE-011
CPPL_TEST(a_directive_inside_a_cpp_l_construct_stays_where_it_stands) {
    static const std::string source = "# 1 \"directives.cpp\"\n"
                                      "law identity(unsigned x)\n"
                                      "    expects (x < 100u)\n"
                                      "#pragma pack(push, 1)\n"
                                      "    proves (x == x);\n"
                                      "proof identity_holds(unsigned x)\n"
                                      "    proves (identity(x))\n"
                                      "{\n"
                                      "#pragma pack(push, 2)\n"
                                      "    refl;\n"
                                      "}\n"
                                      "type Small = unsigned where\n"
                                      "# 40 \"directives.cpp\"\n"
                                      "    (self < 10u);\n"
                                      "#pragma pack(pop)\n"
                                      "#pragma pack(pop)\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(source, "directives.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    const auto projection = cppl::frontend::project(stream, syntax, {});
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(projection.diagnostics.empty());

    // Every directive the lexer passed over is in the program, byte for byte,
    // where it was written: the two in C++L declarations, the line marker in a
    // lowered refinement, and the two after them.
    CPPL_CHECK_EQ(stream.directives().size(), std::size_t{6});
    const auto line_of = [](const std::string& text, std::size_t line) {
        std::size_t begin = 0;
        for (std::size_t skipped = 0; skipped < line && begin != std::string::npos; ++skipped) {
            begin = text.find('\n', begin);
            begin = begin == std::string::npos ? begin : begin + 1;
        }
        return begin == std::string::npos ? std::string() : text.substr(begin, text.find('\n', begin) - begin);
    };
    for (const auto& directive : stream.directives()) {
        const auto line = static_cast<std::size_t>(std::ranges::count(source.substr(0, directive.span.offset), '\n'));
        CPPL_CHECK_EQ(line_of(projection.runtime, line), std::string(stream.spelling(directive.span)));
    }
    // The analysed program states each pragma too, after the C++ it generates.
    CPPL_CHECK(projection.analysis.find("#pragma pack(push, 1)") != std::string::npos);
    CPPL_CHECK(projection.analysis.find("#pragma pack(push, 2)") != std::string::npos);

    cppl::diagnostics::Engine checked;
    const auto erased = cppl::erasure::erase(stream, syntax, projection, checked);
    CPPL_CHECK(erased.report.preserved());
    CPPL_CHECK(!checked.has_errors());

    // A directive blanked with the declaration around it is refused, inside a
    // proof-only span and inside a lowering alike.
    for (std::size_t index = 1; index < 4; ++index) {
        const auto& directive = stream.directives()[index];
        cppl::frontend::Projection tampered = projection;
        tampered.runtime.replace(directive.span.offset, directive.span.length, directive.span.length, ' ');
        cppl::diagnostics::Engine refused;
        const auto report = cppl::erasure::erase(stream, syntax, tampered, refused).report;
        CPPL_CHECK(!report.preserved());
        CPPL_CHECK(refused.has_errors());
        if (index < 3) {
            CPPL_CHECK(!report.directives_kept);
        } else {
            CPPL_CHECK(!report.lowerings_canonical);
        }
    }
}

// SPEC: ERASE-017
CPPL_TEST(a_directive_inside_an_expression_cpp_l_states_is_refused) {
    static const std::string source = "# 1 \"misplaced.cpp\"\n"
                                      "law bounded(unsigned x)\n"
                                      "    proves (x <\n"
                                      "#pragma pack(push, 1)\n"
                                      "            100u || x >= 100u);\n"
                                      "#pragma pack(pop)\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(source, "misplaced.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    const auto projection = cppl::frontend::project(stream, syntax, {});
    CPPL_CHECK_EQ(projection.diagnostics.size(), std::size_t{1});
    if (!projection.diagnostics.empty()) {
        CPPL_CHECK_EQ(projection.diagnostics[0].location.line, std::uint32_t{3});
        CPPL_CHECK(projection.diagnostics[0].message.find("#pragma pack(push, 1)") != std::string::npos);
    }
}

// SPEC: ERASE-018
// TRUST.md TCB-ERASE-012
CPPL_TEST(a_lowering_keeps_the_column_of_what_follows_it) {
    static const std::string source = "# 1 \"columns.cpp\"\n"
                                      "type Small = unsigned where (self < 10u); int after_small;\n"
                                      "type Big = unsigned where (self >= 10u &&\n"
                                      "    self < 1000u); int after_big;\n"
                                      "type Positive = int where (self > 0);\n"
                                      "verified int pick(int raw)\n"
                                      "    ensures (result > 0)\n"
                                      "{\n"
                                      "    if (validate<Positive>(raw)) { return raw; } return 1;\n"
                                      "}\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(source, "columns.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    const auto projection = cppl::frontend::project(stream, syntax, {});
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(projection.diagnostics.empty());

    // Every line after the lowerings keeps its column for each of its bytes.
    for (const std::string_view word : {"int after_small;", "int after_big;", "(raw)) { return raw; }"}) {
        const std::size_t written = source.find(word);
        const std::size_t kept = projection.runtime.find(word);
        CPPL_CHECK(written != std::string::npos);
        CPPL_CHECK(kept != std::string::npos);
        const auto column = [](const std::string& text, std::size_t offset) {
            return offset - (text.rfind('\n', offset) + 1);
        };
        CPPL_CHECK_EQ(column(projection.runtime, kept), column(source, written));
    }
    cppl::diagnostics::Engine checked;
    const auto erased = cppl::erasure::erase(stream, syntax, projection, checked);
    CPPL_CHECK(erased.report.preserved());
    CPPL_CHECK(erased.report.columns_preserved);
}

// SPEC: ERASE-018
CPPL_TEST(a_lowering_that_moves_what_follows_it_is_refused) {
    static const std::string source = "# 1 \"moved.cpp\"\n"
                                      "type Positive = int where (self > 0); int after;\n"
                                      "verified int pick(int raw)\n"
                                      "    ensures (result > 0)\n"
                                      "{\n"
                                      "    if (validate<Positive>(raw)) { return raw; } return 1;\n"
                                      "}\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(source, "moved.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    const auto projection = cppl::frontend::project(stream, syntax, {});
    CPPL_CHECK(!engine.has_errors());
    // The projector refuses it by name, and the validator, which recomputes the
    // lowering, refuses the program it would make.
    CPPL_CHECK_EQ(projection.diagnostics.size(), std::size_t{1});
    cppl::diagnostics::Engine checked;
    const auto report = cppl::erasure::erase(stream, syntax, projection, checked).report;
    CPPL_CHECK(!report.columns_preserved);
    CPPL_CHECK(!report.preserved());
    CPPL_CHECK(report.lowerings_canonical);
}

// SPEC: ERASE-018
// Ordinary C++ after a C++L construct on the same line stands, in the analysed
// program, at the line and the column the program has it at.
CPPL_TEST(the_analysis_resumes_where_the_program_has_the_text) {
    static const std::string source = "# 1 \"resumed.cpp\"\n"
                                      "law same(unsigned x) proves (x == x); int after_law;\n"
                                      "proof same_holds(unsigned x) proves (same(x)) { refl; } int after_proof;\n"
                                      "type Small = unsigned where (self < 10u); int after_refinement;\n"
                                      "type Positive = int where (self > 0);\n"
                                      "template <unsigned N>\n"
                                      "verified unsigned at(unsigned y)\n"
                                      "    ensures (result == N)\n"
                                      "{ return N; } int after_body;\n"
                                      "template unsigned at<1u>(unsigned); int after_instantiation;\n"
                                      "verified int pick(int raw)\n"
                                      "    ensures (result > 0)\n"
                                      "{\n"
                                      "    if (validate<Positive>(raw)) { return raw; } int after_validation = 1;\n"
                                      "    unsigned i = 0u;\n"
                                      "    while (i < 3u) invariant (i <= 3u) { int after_loop = 0; i = i + 1u; }\n"
                                      "    ghost int seen = raw; int after_ghost = 1;\n"
                                      "    unsafe { int after_unsafe = 0; }\n"
                                      "    return 1;\n"
                                      "}\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(source, "resumed.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    const auto projection = cppl::frontend::project(stream, syntax, {});
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(projection.diagnostics.empty());

    const auto analysis = cppl::frontend::lex(projection.analysis, "resumed.cpp");
    std::size_t compared = 0;
    for (const auto& written : stream.tokens()) {
        if (written.kind != cppl::frontend::TokenKind::Identifier || !written.text.starts_with("after_")) {
            continue;
        }
        bool found = false;
        for (const auto& resumed : analysis.tokens()) {
            if (resumed.text == written.text && resumed.kind == written.kind) {
                CPPL_CHECK_EQ(resumed.line, written.line);
                CPPL_CHECK_EQ(resumed.column, written.column);
                found = true;
                break;
            }
        }
        CPPL_CHECK(found);
        ++compared;
    }
    CPPL_CHECK_EQ(compared, std::size_t{9});
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

// SPEC: ERASE-019
// The proof-only text of the analysis is exactly what the program run lacks:
// everything generated, and a ghost declaration, which the analysis keeps as
// written. Nothing the program run has is in it, so nothing ordinary is held to
// the rule for proof-only text, and nothing proof-only escapes it.
CPPL_TEST(the_proof_only_text_is_what_the_program_run_lacks) {
    const std::string text = "# 1 \"ghost.cpp\"\n"
                             "law reflexive(unsigned x)\n"
                             "    proves (x == x);\n"
                             "verified unsigned f(unsigned x)\n"
                             "    ensures (result == x)\n"
                             "{\n"
                             "    ghost unsigned seen = x + 1u;\n"
                             "    unsigned kept = x;\n"
                             "    return kept;\n"
                             "}\n"
                             "int main() { return 0; }\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(text, "ghost.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    CPPL_CHECK(!engine.has_errors());
    const auto projection = cppl::frontend::project(stream, syntax, {});
    const auto& spans = projection.proof_only;
    const auto proof_only = [&spans](std::size_t offset) {
        return std::ranges::any_of(spans,
                                   [offset](const auto& span) { return offset >= span.offset && offset < span.end(); });
    };
    // Ordered, disjoint and not adjacent, as the bridge searches them.
    for (std::size_t index = 1; index < spans.size(); ++index) {
        CPPL_CHECK(spans[index - 1].end() < spans[index].offset);
    }
    // Every byte outside the copied segments is generated, and proof-only.
    std::vector<bool> copied(projection.analysis.size(), false);
    for (const auto& segment : projection.segments) {
        for (std::size_t offset = segment.analysis; offset < segment.analysis + segment.length; ++offset) {
            copied[offset] = true;
        }
    }
    for (std::size_t offset = 0; offset < projection.analysis.size(); ++offset) {
        if (!copied[offset]) {
            CPPL_CHECK(proof_only(offset));
        }
    }
    // The ghost declaration is copied as written, and proof-only all the same.
    const std::size_t ghost = projection.analysis.find("seen = x + 1u;");
    CPPL_CHECK(ghost != std::string::npos);
    CPPL_CHECK(copied[ghost]);
    CPPL_CHECK(proof_only(ghost));
    CPPL_CHECK(proof_only(ghost + std::string_view("seen = x + 1u").size()));
    // What the program run has is not.
    for (const std::string_view runtime : {"unsigned kept = x;", "return kept;", "int main() { return 0; }"}) {
        const std::size_t at = projection.analysis.find(runtime);
        CPPL_CHECK(at != std::string::npos);
        for (std::size_t offset = at; offset < at + runtime.size(); ++offset) {
            CPPL_CHECK(!proof_only(offset));
        }
    }
    // The Law's projection is.
    CPPL_CHECK(proof_only(projection.analysis.find("x == x")));
}
