// A draft has no authority (docs/ARCHITECTURE.md ARCH-LSP-007). What only a
// draft keeps -- a Law or a proof not written whole, a statement the recognizer
// could not read -- is never in a syntax recognized to be compiled or
// formatted, is never offered as evidence, and is refused by elaboration.

#include "cppl/clang/ast.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/admissible.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace cppl;
using namespace cppl::frontend;

namespace {

// Each shape a draft keeps and a compile does not.
constexpr std::array<std::string_view, 6> kDrafts = {
    // A head awaiting its clause, which is ordinary C++ until one is written.
    "law pending(int x)\nint after = 0;\n",
    // A claim awaiting its body.
    "law owed(int x)\n    proves (x == x)\n",
    "proof owed(int x)\n    proves (x == x)\n",
    // A body whose `}` is not written.
    "proof open(int x)\n    proves (x == x)\n{\n    refl;\n",
    // A statement that could not be read, in a body and in an arm.
    "proof garbled(int x)\n    proves (x == x)\n{\n    frobnicate x;\n    refl;\n}\n",
    "proof armed(int x)\n    proves (x == x)\n{\n"
    "    cases x {\n        State::one => { frobnicate; refl; }\n    }\n}\n",
};

// A text, lexed and recognized, kept together because the tokens refer into
// the text.
struct Recognized {
    std::string text;
    std::unique_ptr<TokenStream> stream;
    Syntax syntax;
    bool errors = false;
    std::size_t cursor = 0;
};

std::unique_ptr<Recognized> recognized(std::string_view written, RecognitionMode mode) {
    auto result = std::make_unique<Recognized>();
    result->text = written;
    if (const std::size_t cursor = result->text.find('|'); cursor != std::string::npos) {
        result->text.erase(cursor, 1);
        result->cursor = cursor;
    }
    result->stream = std::make_unique<TokenStream>(lex(result->text, "main.cpp"));
    diagnostics::Engine engine;
    result->syntax = recognize(*result->stream, engine, mode);
    result->errors = engine.has_errors();
    return result;
}

bool reports(const diagnostics::Engine& engine, std::string_view message) {
    return std::ranges::any_of(engine.diagnostics(), [message](const diagnostics::Diagnostic& diagnostic) {
        return diagnostic.message == message;
    });
}

// The names offered as evidence at the cursor, which must be just after a word
// that names evidence.
std::string offered(const Recognized& draft) {
    const Admissible here = admissible_at(*draft.stream, draft.syntax, draft.cursor);
    if (!here.proof.has_value() || !here.evidence_for.has_value()) {
        return "no evidence asked for";
    }
    std::string names;
    for (const Evidence& evidence : evidence_at(draft.syntax, *here.proof, draft.cursor)) {
        names += (names.empty() ? "" : " ") + evidence.name;
    }
    return names;
}

constexpr std::string_view kRefused = "an editor's draft of the text reached elaboration";

// Elaborates `syntax` alone. The guard under test runs before elaboration
// reads the projection or the unit, so empty ones stand in for Clang's.
bool elaboration_refuses(const Syntax& syntax) {
    const Projection projection;
    const clangbridge::TranslationUnit unit;
    diagnostics::Engine engine;
    const elaboration::Result result = elaboration::elaborate(elaboration::Request{syntax, projection, unit}, engine);
    return reports(engine, kRefused) && result.module.laws.empty() && result.module.proofs.empty();
}

} // namespace

CPPL_TEST(only_a_draft_holds_what_is_not_written_whole) {
    for (std::size_t index = 0; index < kDrafts.size(); ++index) {
        const std::string_view text = kDrafts[index];
        CPPL_CHECK(draft_only(recognized(text, RecognitionMode::Draft)->syntax).has_value());
        CPPL_CHECK(!draft_only(recognized(text, RecognitionMode::Edit)->syntax).has_value());
        const auto compiled = recognized(text, RecognitionMode::Compile);
        CPPL_CHECK(!draft_only(compiled->syntax).has_value());
        // A compile refuses every shape but the head, which is C++.
        CPPL_CHECK_EQ(compiled->errors, index != 0);
    }
}

CPPL_TEST(nothing_only_a_draft_keeps_is_offered_as_evidence) {
    // A trusted Law written whole is offered; one still awaiting its `;` is
    // not, and an `assume` that could not be read binds nothing.
    const auto after = recognized("trusted law axiom(int y)\n    proves (y == y);\n"
                                  "proof mine(int x)\n    proves (x == x)\n{\n"
                                  "    assume h x == x;\n"
                                  "    exact | }\n"
                                  "trusted law pending(int y)\n    proves (y == y)\n",
                                  RecognitionMode::Draft);
    CPPL_CHECK(draft_only(after->syntax).has_value());
    CPPL_CHECK_EQ(offered(*after), std::string("axiom"));

    // A proof written whole is offered; one whose body is not closed is not.
    const auto before = recognized("proof done(int y)\n    proves (y == y)\n{\n    refl;\n}\n"
                                   "proof earlier(int y)\n    proves (y == y)\n{\n    refl;\n"
                                   "proof mine(int x)\n    proves (x == x)\n{\n    exact |",
                                   RecognitionMode::Draft);
    CPPL_CHECK_EQ(offered(*before), std::string("done"));
}

CPPL_TEST(elaboration_refuses_a_draft) {
    for (const std::string_view text : kDrafts) {
        CPPL_CHECK(elaboration_refuses(recognized(text, RecognitionMode::Draft)->syntax));
    }
    // A whole text passes the guard, as the compiler recognizes it.
    const auto whole =
        recognized("proof whole(int x)\n    proves (x == x)\n{\n    refl;\n}\n", RecognitionMode::Compile);
    CPPL_CHECK(!whole->errors);
    CPPL_CHECK(!elaboration_refuses(whole->syntax));
}
