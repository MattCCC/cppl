#include "pipeline.hpp"

#include "cppl/analysis/analyze.hpp"
#include "cppl/automation/evidence.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/erasure/erase.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/source/digest.hpp"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <vector>

namespace cppl::driver::detail {

namespace {

void report(diagnostics::Engine& engine, diagnostics::Category category, std::string message,
            source::SourceLocation location = {}, std::string note = {}) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = category;
    diagnostic.message = std::move(message);
    diagnostic.location = std::move(location);
    if (!note.empty()) {
        diagnostic.notes.push_back(diagnostics::Note{std::move(note), diagnostic.location});
    }
    engine.report(std::move(diagnostic));
}

diagnostics::Severity convert(clangbridge::Severity severity) {
    switch (severity) {
        case clangbridge::Severity::Note:
            return diagnostics::Severity::Note;
        case clangbridge::Severity::Warning:
            return diagnostics::Severity::Warning;
        case clangbridge::Severity::Error:
        case clangbridge::Severity::Fatal:
            return diagnostics::Severity::Error;
    }
    return diagnostics::Severity::Error;
}

} // namespace

PipelineOutcome run_pipeline(const PipelineRequest& request, diagnostics::Engine& engine) {
    PipelineOutcome outcome;

    const frontend::TokenStream stream = frontend::lex(request.preprocessed_text, request.original_path);
    frontend::Syntax syntax = frontend::recognize(stream, engine);

    if (engine.has_errors()) {
        // recognize() still returns whatever it built before the error, so a
        // caller such as the LSP's structural linter keeps a usable (if
        // partial) syntax tree instead of losing all C++L-aware tooling the
        // moment one construct is malformed (README.md, "Failure isolation").
        outcome.has_cppl = !syntax.empty();
        outcome.tokens = std::make_unique<frontend::TokenStream>(stream);
        outcome.syntax = std::make_unique<frontend::Syntax>(std::move(syntax));
        outcome.failed = true;
        return outcome;
    }

    if (syntax.empty()) {
        // Ordinary C++: nothing to verify (ARCHITECTURE.md 29).
        outcome.tokens = std::make_unique<frontend::TokenStream>(stream);
        outcome.syntax = std::make_unique<frontend::Syntax>(std::move(syntax));
        if (request.check_ordinary_cpp_with_clang) {
            clangbridge::ParseRequest parse_request;
            parse_request.path = request.original_path;
            parse_request.arguments = request.clang_arguments;
            parse_request.arguments.emplace_back("-w");
            // The original path may not exist on disk as this exact text
            // (a live editor buffer): parse the preprocessed text from a
            // scratch file instead, exactly as the C++L path below does for
            // its analysis projection, so #line-mapped locations still
            // point at request.original_path.
            const std::filesystem::path scratch_source = request.scratch / (request.stem + ".ordinary.cpp");
            if (!write_scratch_file(scratch_source, request.preprocessed_text)) {
                report(engine, diagnostics::Category::Internal,
                       "could not write a scratch copy of '" + request.original_path + "'");
                outcome.failed = true;
                return outcome;
            }
            parse_request.path = scratch_source.string();
            parse_request.arguments.emplace_back("-x");
            parse_request.arguments.emplace_back("c++-cpp-output");

            const std::expected<clangbridge::TranslationUnit, std::string> unit = clangbridge::parse(parse_request);
            if (!unit.has_value()) {
                report(engine, diagnostics::Category::Internal, unit.error());
                outcome.failed = true;
                return outcome;
            }
            for (const clangbridge::Diagnostic& diagnostic : unit->diagnostics) {
                if (diagnostic.severity != clangbridge::Severity::Error &&
                    diagnostic.severity != clangbridge::Severity::Fatal) {
                    continue;
                }
                diagnostics::Diagnostic converted;
                converted.severity = convert(diagnostic.severity);
                converted.category = diagnostics::Category::CppSemantic;
                converted.message = diagnostic.message;
                converted.location = diagnostic.location;
                // A location the bridge could not map back through #line
                // reports the scratch path; that is useless to a caller
                // matching diagnostics to the original document, so it is
                // replaced with the original path at an unknown line rather
                // than silently pointing at a file that will be deleted.
                if (converted.location.file == parse_request.path) {
                    converted.location.file = request.original_path;
                }
                engine.report(std::move(converted));
            }
            if (unit->has_errors) {
                outcome.failed = true;
            }
        }
        return outcome;
    }

    outcome.has_cppl = true;

    if (request.reject_if_cppl) {
        if (std::optional<PipelineRequest::Rejection> rejection = request.reject_if_cppl(syntax)) {
            report(engine, diagnostics::Category::UnsupportedSemantics, rejection->message, {}, rejection->note);
            outcome.tokens = std::make_unique<frontend::TokenStream>(stream);
            outcome.syntax = std::make_unique<frontend::Syntax>(std::move(syntax));
            outcome.failed = true;
            return outcome;
        }
    }

    frontend::ProjectionOptions projection_options;
    projection_options.unit_key =
        source::hash_bytes(std::filesystem::absolute(request.original_path).string()).to_short_hex(12);

    // A first projection, only to report its own diagnostics and to get a
    // stable analysis/runtime path pair on disk before analysis::analyze
    // iterates on the projection to resolve proof binding types
    // (compiler/analysis/include/cppl/analysis/analyze.hpp). The projection
    // it eventually settles on, not this one, is what elaboration uses.
    const frontend::Projection initial_projection = frontend::project(stream, syntax, projection_options);
    for (const auto& diagnostic : initial_projection.diagnostics)
        engine.report(diagnostic);
    if (engine.has_errors()) {
        outcome.tokens = std::make_unique<frontend::TokenStream>(stream);
        outcome.syntax = std::make_unique<frontend::Syntax>(std::move(syntax));
        outcome.failed = true;
        return outcome;
    }

    const std::filesystem::path analysis_path = request.scratch / (request.stem + ".analysis.cpp");
    const std::filesystem::path runtime_path = request.scratch / (request.stem + ".runtime.cpp");
    if (!write_scratch_file(analysis_path, initial_projection.analysis) ||
        !write_scratch_file(runtime_path, initial_projection.runtime)) {
        report(engine, diagnostics::Category::Internal,
               "could not write the projection of '" + request.original_path + "'");
        outcome.tokens = std::make_unique<frontend::TokenStream>(stream);
        outcome.syntax = std::make_unique<frontend::Syntax>(std::move(syntax));
        outcome.failed = true;
        return outcome;
    }

    clangbridge::ParseRequest parse_request;
    parse_request.path = analysis_path.string();
    parse_request.arguments = request.clang_arguments;
    parse_request.arguments.emplace_back("-x");
    parse_request.arguments.emplace_back("c++-cpp-output");
    parse_request.arguments.emplace_back("-w");

    const std::expected<analysis::Result, std::string> analyzed =
        analysis::analyze(stream, syntax, projection_options, parse_request);
    outcome.tokens = std::make_unique<frontend::TokenStream>(stream);
    outcome.syntax = std::make_unique<frontend::Syntax>(syntax);
    if (!analyzed.has_value()) {
        report(engine, diagnostics::Category::Internal, analyzed.error());
        outcome.failed = true;
        return outcome;
    }

    // analyze() may have iterated the projection to resolve proof binding
    // types; the resolved analysis text is what Clang actually parsed, so it
    // replaces what was written above before anything downstream reads the
    // scratch files (driver.cpp's prior behaviour, preserved exactly).
    const frontend::Projection& projection = analyzed->projection;
    if (!write_scratch_file(analysis_path, projection.analysis)) {
        report(engine, diagnostics::Category::Internal, "could not write resolved analysis projection");
        outcome.failed = true;
        return outcome;
    }

    const clangbridge::TranslationUnit& unit = analyzed->unit;
    for (const clangbridge::Diagnostic& diagnostic : unit.diagnostics) {
        if (diagnostic.severity != clangbridge::Severity::Error &&
            diagnostic.severity != clangbridge::Severity::Fatal) {
            continue;
        }
        diagnostics::Diagnostic converted;
        converted.severity = convert(diagnostic.severity);
        converted.category = diagnostics::Category::CppSemantic;
        converted.message = diagnostic.message;
        converted.location = diagnostic.location;
        engine.report(std::move(converted));
    }
    if (unit.has_errors) {
        outcome.failed = true;
        return outcome;
    }

    const elaboration::Result elaborated =
        elaboration::elaborate(elaboration::Request{syntax, projection, unit}, engine);

    const obligations::Program program = obligations::generate(elaborated.module, elaborated, engine);
    if (!engine.has_errors() && program.proofs.size() != syntax.proofs.size()) {
        report(engine, diagnostics::Category::Internal, "not every written proof produced explicit evidence");
    }
    const std::vector<obligations::ObligationResult> results = automation::verify(program, engine);

    outcome.counters.laws += elaborated.module.laws.size();
    std::size_t declaration_obligations = 0;
    for (const obligations::ObligationResult& result : results) {
        if (result.obligation.origin == obligations::Origin::LawProposition ||
            result.obligation.origin == obligations::Origin::FunctionContract) {
            ++declaration_obligations;
        }
        if (result.verdict.is_proven()) {
            if (result.obligation.origin == obligations::Origin::FunctionContract) {
                ++outcome.counters.contracts_proven;
            } else if (result.obligation.origin == obligations::Origin::CallPrecondition) {
                ++outcome.counters.call_preconditions_proven;
            } else if (result.obligation.origin == obligations::Origin::LawProposition) {
                ++outcome.counters.proven;
            } else if (result.obligation.origin == obligations::Origin::ProofProposition) {
                ++outcome.counters.proofs_proven;
            } else if (result.obligation.origin == obligations::Origin::LoopEntry ||
                       result.obligation.origin == obligations::Origin::LoopPreservation) {
                ++outcome.counters.loop_invariants_proven;
            } else if (result.obligation.origin == obligations::Origin::LoopDescent) {
                ++outcome.counters.loop_measures_proven;
            }
            if (result.obligation.law && program.proof_for(result.obligation) != nullptr) {
                ++outcome.counters.proven_by_written_proof;
            }
        } else if (result.verdict.is_trusted()) {
            // An explicit assumption is neither proven nor unresolved: it is a
            // recorded gap (SPEC.md 27.1). Counting it as unresolved would fail
            // the build; counting it as proven would hide it.
            outcome.counters.trusted.push_back(result.obligation.subject + " (" + result.verdict.reason() + ")");
        } else {
            ++outcome.counters.unresolved;
        }
    }
    // A partial-correctness contract has no single obligation of its own: it is
    // established when every one of its conditions is proven.
    for (const obligations::ContractVerification& contract : program.contracts) {
        if (!contract.partial) {
            continue;
        }
        ++declaration_obligations;
        if (std::ranges::all_of(contract.conditions, [&results](const obligations::VerificationCondition& condition) {
                return results[condition.obligation].verdict.is_proven();
            })) {
            ++outcome.counters.contracts_proven;
            ++outcome.counters.partial_contracts_proven;
        }
    }
    const std::size_t required = syntax.laws.size() + syntax.verified_functions.size();
    if (declaration_obligations < required) {
        outcome.counters.unresolved += required - declaration_obligations;
        if (!engine.has_errors()) {
            report(engine, diagnostics::Category::Internal,
                   "not every formal declaration produced a verification obligation");
        }
    }

    const erasure::Erased erased = erasure::erase(stream, syntax, projection, engine);
    if (!erased.report.only_deletions || !erased.report.lines_preserved) {
        outcome.failed = true;
        return outcome;
    }

    if (engine.has_errors()) {
        outcome.failed = true;
        return outcome;
    }

    if (!request.emit_projection_path.empty() && !write_scratch_file(request.emit_projection_path, erased.runtime)) {
        report(engine, diagnostics::Category::Internal,
               "could not write the runtime projection to '" + request.emit_projection_path + "'");
        outcome.failed = true;
        return outcome;
    }

    outcome.runtime_path = runtime_path.string();
    return outcome;
}

} // namespace cppl::driver::detail
