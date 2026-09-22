#pragma once

#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/lsp/protocol.hpp"

#include <cstddef>
#include <optional>
#include <vector>

// What an editor needs to know about one `cases`/`decompose` statement.
//
// Every fact here was decided by the compiler: the states come from the generic
// case engine's own record (`elaboration::SubjectStates`), and the arms come
// from the recognizer's syntax tree. This header adds no decomposition,
// exhaustiveness or label-resolution logic of its own -- `AGENTS.md` 39 allows
// exactly one case engine, and it is the compiler's
// (`tools/cppl-lsp/README.md`).
//
// A consequence worth stating: when the pipeline cannot reach elaboration (no
// Clang, a broken include, a syntax error earlier in the file), there are no
// recorded states and completion offers nothing rather than guessing. An
// editor suggestion is never worth inventing a state the compiler did not
// confirm.
namespace cppl::lsp {

// One `cases`/`decompose` statement, with what the compiler knows about it.
struct CaseSite {
    // The statement's own syntax, owned by the document's parse.
    const frontend::ProofStatement* statement = nullptr;
    // The engine's record for this statement, when elaboration reached it and
    // a provider modeled the subject. Null when it did not.
    const elaboration::SubjectStates* states = nullptr;
};

// The `cases`/`decompose` statement whose arm block encloses `offset`, or null
// when the offset is not inside one.
//
// Nesting is resolved innermost-first: `cases` inside an arm of another
// `cases` is the site an editor is completing in, which is what
// `ProofArm::body_span` containment already expresses.
[[nodiscard]] std::optional<CaseSite> enclosing_case_site(const frontend::Syntax& syntax,
                                                          const std::vector<elaboration::SubjectStates>& recorded,
                                                          std::size_t offset);

// The returned site borrows from both arguments, so neither may be a
// temporary. Binding one would leave `CaseSite::states` dangling at the end of
// the full expression, which is a bug a caller cannot see. Deleted rather than
// documented.
std::optional<CaseSite> enclosing_case_site(const frontend::Syntax&, std::vector<elaboration::SubjectStates>&&,
                                            std::size_t) = delete;
std::optional<CaseSite> enclosing_case_site(frontend::Syntax&&, const std::vector<elaboration::SubjectStates>&,
                                            std::size_t) = delete;

// The labels `site` still admits at `offset`: every state the provider listed,
// minus the ones this statement already has an arm for.
//
// Each item carries the provider's own binder names, so an accepted completion
// inserts an arm whose binder count already matches the state's payload. A
// residual state is offered like any other: it is a real semantic state, not a
// catch-all (`AGENTS.md` 39, no wildcard).
[[nodiscard]] std::vector<CompletionItem> missing_case_completions(const CaseSite& site);

// What to show when hovering `offset` inside a case site: the subject's
// resolved representation, which provider modeled it, and the full state
// partition with the covered ones marked. Empty when there is nothing
// confirmed to say.
[[nodiscard]] std::optional<Hover> case_site_hover(const CaseSite& site);

} // namespace cppl::lsp
