#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cppl::driver {

// A request to run the C++L compile pipeline over an in-memory buffer rather
// than a file already sitting on disk with stable content, e.g. a live
// editor document `cppl-lsp` owns (tools/cppl-lsp/README.md).
struct BufferCompileRequest {
    // A filesystem-realistic path for the buffer: used for #include
    // resolution relative to the file, and reported in diagnostics and
    // #line markers, exactly like a real input path would be. It need not
    // exist on disk; only its directory is used to resolve includes, and
    // Clang is pointed at a scratch copy of `text`, never at this path.
    std::string virtual_path;

    // The document's live text, exactly as the editor holds it right now.
    std::string text;

    // The Clang driver executable and any extra arguments (include paths,
    // defines, target flags, -std=) to preprocess and semantically check
    // the buffer with. Mirrors cppl::driver::Options::clang / arguments,
    // minus input/output selection, which this request supplies itself.
    std::string clang;
    std::vector<std::string> clang_arguments;
};

struct BufferCompileOutcome {
    // False when the pipeline itself could not run (preprocessing failed to
    // start, clang was not found, scratch files could not be written, ...).
    // A rejected/erroneous *program* is not a pipeline failure: its
    // diagnostics are in the engine like any other, and `ok` stays true.
    bool ok = true;

    // Whether the buffer was recognized as containing any C++L syntax at
    // all. When false, the buffer is ordinary C++ and was (or would be)
    // compiled directly by Clang with no projection involved
    // (ARCHITECTURE.md 29).
    bool has_cppl = false;

    // The preprocessed text `tokens` and `syntax` were recognized from. They
    // refer into it rather than copying it, so the outcome owns it, on the
    // heap, where moving the outcome cannot move it. Declared before them so
    // it is destroyed after them.
    std::unique_ptr<const std::string> text;

    // Set whenever recognition ran far enough to produce a result (i.e.
    // whenever preprocessing itself succeeded), independent of `has_cppl` or
    // whether later stages failed, so a caller such as the LSP's structural
    // linter can keep working from the same recognition the pipeline
    // performed instead of re-lexing the buffer itself.
    std::unique_ptr<frontend::TokenStream> tokens;
    std::unique_ptr<frontend::Syntax> syntax;

    // The states each `cases`/`decompose` subject was found to have, recorded
    // by the one generic engine while it elaborated them. Empty when the
    // pipeline stopped before elaboration, or when no statement's subject was
    // modeled by a provider. An editor reads this to offer exactly the labels
    // the compiler would accept; it never recomputes them.
    std::vector<elaboration::SubjectStates> subject_states;
};

// Runs preprocess -> recognize -> project -> Clang parse -> elaborate ->
// obligations -> verify -> erase over a live buffer, exactly as
// cppl::driver::run_driver does for a file, except that the buffer is
// written to a scratch file first instead of already existing on disk with
// stable content (tools/cppl-lsp/README.md, "Projection and source
// mapping"). Every diagnostic produced by any stage is reported into
// `engine`: C++L syntax diagnostics, Clang semantic diagnostics (mapped
// through #line back to `request.virtual_path`), and elaboration/obligation
// diagnostics.
//
// This function never throws and never crashes on a malformed buffer,
// missing clang, or a broken #include: those are reported as diagnostics
// with `ok` left true, since the pipeline itself still ran to completion.
[[nodiscard]] BufferCompileOutcome compile_buffer(const BufferCompileRequest& request, diagnostics::Engine& engine);

} // namespace cppl::driver
