#include "pipeline.hpp"

#include "cppl/analysis/analyze.hpp"
#include "cppl/automation/evidence.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/erasure/erase.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/obligations/contracts.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/status.hpp"
#include "cppl/obligations/trust.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
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

// A file the preprocessor read, as it is on disk, so tokens can be given the
// columns their author wrote them at rather than the preprocessor's.
std::optional<std::string> written_text(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace

PipelineOutcome run_pipeline(const PipelineRequest& request, diagnostics::Engine& engine) {
    PipelineOutcome outcome;

    frontend::TokenStream stream = frontend::lex(request.preprocessed_text, request.original_path);
    stream.use_written_columns([&request](const std::string& file) -> std::optional<std::string> {
        if (request.original_text.has_value() && file == request.original_path) {
            return std::string(*request.original_text);
        }
        return written_text(file);
    });
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
        // Ordinary C++: nothing to verify (ARCHITECTURE.md 81).
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
    // stable analysis path on disk before analysis::analyze iterates on the
    // projection to resolve proof binding types
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
    if (!write_scratch_file(analysis_path, initial_projection.analysis)) {
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
    outcome.subject_states = elaborated.subject_states;
    outcome.names = elaborated.names;

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
            } else if (result.obligation.origin == obligations::Origin::OmittedCase) {
                ++outcome.counters.omitted_cases_proven;
            } else if (result.obligation.origin == obligations::Origin::ImpossiblePath) {
                ++outcome.counters.impossible_paths_proven;
            }
            if (result.obligation.law && program.proof_for(result.obligation) != nullptr) {
                ++outcome.counters.proven_by_written_proof;
            }
        } else if (result.verdict.is_trusted()) {
            // An explicit assumption is neither proven nor unresolved: it is a
            // recorded gap (SPEC.md 27.1). Counting it as unresolved would fail
            // the build; counting it as proven would hide it. The trust closure
            // below names it.
            ++outcome.counters.trusted;
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
    // Every claim counted as proven above has a trust closure, and none is
    // counted without one. A report that could not attribute a dependency, or
    // that lost a claim on the way, would show fewer assumptions than the build
    // rests on, so either is an internal error rather than a shorter report
    // (TRUST.md 2.10, TCB-REPORT-002, TCB-REPORT-006).
    outcome.counters.closure = obligations::close_trust(program, results);
    const auto claims = [&outcome](obligations::ClaimKind kind) {
        return static_cast<std::size_t>(
            std::ranges::count(outcome.counters.closure.claims, kind, &obligations::ClaimClosure::kind));
    };
    if (outcome.counters.closure.assumptions.size() != outcome.counters.trusted) {
        outcome.counters.closure.faults.emplace_back("a trusted law is not reported as TRUSTED");
    }
    if (claims(obligations::ClaimKind::Law) != outcome.counters.proven ||
        claims(obligations::ClaimKind::Proof) != outcome.counters.proofs_proven ||
        claims(obligations::ClaimKind::Contract) != outcome.counters.contracts_proven ||
        claims(obligations::ClaimKind::OmittedCase) != outcome.counters.omitted_cases_proven ||
        claims(obligations::ClaimKind::ImpossiblePath) != outcome.counters.impossible_paths_proven) {
        outcome.counters.closure.faults.emplace_back("a proven claim has no trust closure");
    }
    for (const std::string& fault : outcome.counters.closure.faults) {
        report(engine, diagnostics::Category::Internal,
               "the trusted laws a claim rests on cannot be reported: " + fault);
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
    if (!erased.report.preserved()) {
        outcome.failed = true;
        return outcome;
    }

    if (engine.has_errors()) {
        outcome.failed = true;
        return outcome;
    }

    // What Clang compiles is the text erasure has just checked, written only
    // now, so no earlier projection of the unit can stand in for it
    // (TCB-ERASE-006).
    //
    // The runtime program is preprocessed C++, and `.ii` says so to Clang as
    // well as `-x` does. Clang then names the compile unit in debug information
    // after the source its first line marker names, the user's own file, rather
    // than after this scratch file, which is gone once the build ends and whose
    // random directory would differ in every object built (ARCH-ERASE-003).
    const std::filesystem::path runtime_path = request.scratch / (request.stem + ".runtime.ii");
    if (!write_scratch_file(runtime_path, erased.runtime)) {
        report(engine, diagnostics::Category::Internal, "could not write the runtime program");
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
