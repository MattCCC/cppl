#pragma once

#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cppl::frontend {

// What C++L admits where text is being written, read from the recognizer's
// draft of that text (RecognitionMode::Draft).
//
// An editor offers C++L from this and never reads C++L's grammar itself: the
// recognizer is the one authority for what C++L is, so an editor and the
// compiler never disagree about where a declaration, a clause or a proof
// statement may be written (tools/cppl-lsp/README.md, "One C++L frontend").
// Ordinary C++ scope is Clang's to say: a declaration admitted here is still
// C++L only at namespace scope.
struct Admissible {
    // A declaration may begin here: the recognizer would read one, and no
    // Law or proof holds the position.
    bool declaration = false;

    // The clauses that may be written here, after a declaration's parameters
    // or one of its clauses, in the grammar's order; and what they would be
    // written on.
    std::vector<ClauseKind> clauses;
    ClauseOwner owner = ClauseOwner::Law;

    // The proof whose body holds the position, as an index into
    // Syntax::proofs.
    std::optional<std::size_t> proof;
    // A proof statement may begin here.
    bool statement = false;
    // The statement begun here names its evidence next.
    std::optional<ProofStatementKind> evidence_for;
};

// What may be written at byte `offset` of the text `stream` was lexed from,
// `draft` being the recognizer's draft of the same text. `offset` is where
// what is being written starts.
[[nodiscard]] Admissible admissible_at(const TokenStream& stream, const Syntax& draft, std::size_t offset);

// A name a statement at `offset` in the body of `draft.proofs[proof]` may name
// as its evidence, looked for as elaboration looks for it: a premise this body
// assumed before `offset`, in the block that holds `offset` or one enclosing
// it, the innermost first; then a trusted Law; then any other proof, a Law
// proved where it is declared included. A proof never names itself.
struct Evidence {
    enum class Kind : std::uint8_t {
        Assumption,
        TrustedLaw,
        Proof,
    };
    Kind kind = Kind::Proof;
    std::string name;
};

[[nodiscard]] std::vector<Evidence> evidence_at(const Syntax& draft, std::size_t proof, std::size_t offset);

} // namespace cppl::frontend
