#pragma once

#include "cppl/clang/ast.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cppl::elaboration {

// A declaration C++L could not give formal meaning to, and why.
//
// The reason is kept rather than discarded so that a law depending on this
// declaration can explain itself instead of failing anonymously.
struct FunctionRejection {
    vir::SymbolId symbol;
    std::string name;
    std::string reason;
    source::SourceLocation location;
};

// The states one `cases`/`decompose` subject was found to have.
//
// This is a *record* of what the generic engine already decided while
// elaborating the statement, kept so that a non-compiling consumer -- the
// language server -- can offer exactly the labels the compiler would accept
// without running a decomposition of its own. Nothing reads it back into
// elaboration, and a stale or missing entry can only cost an editor
// suggestion: it is never proof-relevant (`AGENTS.md` 39, one engine).
struct SubjectStates {
    // Where the `cases`/`decompose` keyword was written.
    source::SourceLocation location;
    // The subject exactly as the author spelled it.
    std::string subject;
    // The resolved C++ type the provider recognized.
    std::string representation;
    // The provider that modeled it, for the editor to name its source.
    std::string provider;
    // True for a product (one `components(...)` arm), false for a sum.
    bool product = false;

    struct State {
        std::string label;
        // The provider's own binder names for this state, in order.
        std::vector<std::string> binders;
        // The state completing the partition, which the engine derives by
        // negating the others rather than the provider supplying it.
        bool residual = false;
    };
    std::vector<State> states;
};

// A name a proof statement uses, as elaboration resolved it: `exact p;`,
// `apply p;`, `rewrite h;`, `contradiction e;` in a proof or in a verified body,
// and the evidence an omitted case names. Editors navigate by these; nothing
// in the compiler reads them back, so they cannot change which proofs are
// accepted.
struct ResolvedName {
    enum class Kind : std::uint8_t {
        Proof,
        TrustedLaw,
        // A name an earlier `assume` in the same body bound.
        Assumption,
    };
    Kind kind = Kind::Proof;
    std::string name;
    // Where the statement writes the name.
    source::SourceLocation at;
    // Where what it names is declared: the proof's or the Law's name, or the
    // name `assume` binds.
    source::SourceLocation declaration;
};

struct Result {
    vir::Module module;
    std::vector<FunctionRejection> rejected_functions;

    // Every name a proof statement uses that elaboration resolved, in the order
    // it resolved them.
    std::vector<ResolvedName> names;

    // One entry per `cases`/`decompose` statement whose subject a provider
    // modeled, in source order. Statements the provider boundary refused are
    // absent: there is nothing to suggest for a representation with no model.
    std::vector<SubjectStates> subject_states;

    // Laws an author wrote a proof for, where that proof was refused.
    //
    // The law is not left to be closed by the compiler's own strategy: an
    // author who wrote evidence has said how the law is to be established, and
    // a refused proof is a failure of the law, not an invitation to try
    // something else.
    std::vector<vir::LawId> laws_with_refused_proofs;

    [[nodiscard]] const FunctionRejection* rejection(const vir::SymbolId& symbol) const;
};

struct Request {
    const frontend::Syntax& syntax;
    const frontend::Projection& projection;
    const clangbridge::TranslationUnit& unit;
};

[[nodiscard]] std::optional<vir::Type> resolved_type(const clangbridge::Type& type);

// Connects C++L constructs to the C++ semantics Clang resolved, and produces
// the typed VIR those constructs mean (ARCHITECTURE.md 14).
//
// Elaboration is not a proof authority: everything it produces is checked
// later, and anything it cannot model is reported rather than approximated.
[[nodiscard]] Result elaborate(const Request& request, diagnostics::Engine& engine);

} // namespace cppl::elaboration
