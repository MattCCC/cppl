#pragma once

// The part of the compile pipeline that is common to every caller once
// preprocessed C++L text is available: recognize -> project -> parse with
// Clang -> elaborate -> generate obligations -> verify -> erase.
//
// The CLI (driver.cpp, compile_unit) and the LSP buffer path
// (buffer_compile.cpp, compile_buffer) differ only in how the preprocessed
// text is obtained: compile_unit shells out to `clang -E` against a real
// file and argv; compile_buffer writes a live editor buffer to a scratch
// file first. Everything from frontend::lex onward is identical, and lives
// here so the two callers cannot drift apart (AGENTS.md 14, 27).
//
// This header is a private implementation detail of cppl_driver: it is not
// installed and no other library includes it.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cppl::driver::detail {

struct PipelineRequest {
    // The preprocessed text, and the path it is reported as (the original
    // input for the CLI; the document's virtual path for the LSP).
    std::string_view preprocessed_text;
    std::string original_path;

    // Where scratch projections may be written (already created).
    std::filesystem::path scratch;
    std::string stem; // basename used for scratch file names

    // Clang invocation shared by preprocessing and semantic analysis, with
    // input/output selection already stripped (driver::base_arguments).
    std::string clang;
    std::vector<std::string> clang_arguments;

    // Whether the caller wants the runtime projection's text written out to
    // `emit_projection_path` (CLI's --emit-projection). Empty means no.
    std::string emit_projection_path;

    // A diagnostic a caller wants reported, with an explanatory note, when
    // recognition produces non-empty C++L syntax before projection proceeds.
    // Lets the CLI reject a case recognition alone cannot rule out (a header,
    // or -x, containing C++L constructs) without recognizing the text a
    // second time.
    struct Rejection {
        std::string message;
        std::string note;
    };
    // Called once recognition has produced non-empty C++L syntax. Returning
    // an engaged optional reports it as an UnsupportedSemantics error and
    // stops the pipeline there, as if projection had failed.
    std::function<std::optional<Rejection>(const frontend::Syntax&)> reject_if_cppl;

    // When true and the text has no C++L syntax at all, the pipeline still
    // runs a plain Clang parse over the preprocessed text and reports its
    // diagnostics, instead of returning immediately. The CLI does not need
    // this: an ordinary-C++ input is compiled for real by the native
    // compiler at the end of run_driver, so parsing it here too would be
    // redundant and could report through different flags than the final
    // compile uses. The LSP has no later "real compile" step of its own, so
    // it needs this pipeline to be the source of C++ diagnostics even for a
    // file with no C++L in it at all (tools/cppl-lsp/README.md: "Ordinary
    // C++ remains ordinary C++" -- the same semantic treatment it would get
    // through Clang).
    bool check_ordinary_cpp_with_clang = false;
};

struct PipelineOutcome {
    bool failed = false;
    bool has_cppl = false;

    // Set when has_cppl: the tokens and recognized syntax, so a caller such
    // as the LSP's structural linter can keep working from the same
    // recognition the compiler itself performed. Owned by the outcome so it
    // outlives the pipeline call.
    std::unique_ptr<frontend::TokenStream> tokens;
    std::unique_ptr<frontend::Syntax> syntax;

    // What the generic case engine decided each `cases`/`decompose` subject's
    // states were, for editors to offer. A byproduct of elaboration: no later
    // stage reads it back.
    std::vector<elaboration::SubjectStates> subject_states;

    // The runtime program's scratch path, when one was produced (has_cppl
    // and not failed before erasure). Empty otherwise.
    std::string runtime_path;

    // Aggregate verification counts, for callers that report a trust summary
    // (the CLI). The LSP does not need these and may ignore them.
    struct Counters {
        std::size_t laws = 0;
        std::size_t proven = 0;
        std::size_t contracts_proven = 0;
        std::size_t partial_contracts_proven = 0;
        std::size_t loop_invariants_proven = 0;
        // Descent obligations discharged. Reported apart from invariants
        // because they are what makes a loop total rather than partial
        // (SPEC.md 23, CORRECT-006).
        std::size_t loop_measures_proven = 0;
        // Impossibility claims, each counted under its own origin: one checked
        // contradiction mechanism discharges both, but an omitted case and an
        // unreachable runtime path are different claims (SPEC.md CASE-012,
        // CASE-016).
        std::size_t omitted_cases_proven = 0;
        std::size_t impossible_paths_proven = 0;
        std::size_t call_preconditions_proven = 0;
        std::size_t proven_by_written_proof = 0;
        std::size_t proofs_proven = 0;
        std::size_t unresolved = 0;
        // Every explicit trusted assumption, named so the trust report can list
        // it rather than only count it (SPEC.md 27, TRUST.md 29).
        std::vector<std::string> trusted;
    } counters;
};

// Runs recognize -> project -> Clang parse -> elaborate -> obligations ->
// verify -> erase over already-preprocessed text. Every diagnostic (C++L
// syntax, Clang semantic, elaboration, proof) is reported into `engine`.
//
// Returns early with has_cppl == false, failed == false when the text has no
// C++L syntax at all: ordinary C++ is not this pipeline's concern past
// recognition (compile_unit's existing behaviour, preserved exactly).
[[nodiscard]] PipelineOutcome run_pipeline(const PipelineRequest& request, diagnostics::Engine& engine);

} // namespace cppl::driver::detail
