#include "pipeline.hpp"

#include "cppl/analysis/analyze.hpp"
#include "cppl/artifact/interface.hpp"
#include "cppl/automation/evidence.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/buffer_compile.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/erasure/erase.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/obligations/contracts.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/obligations/interface.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/status.hpp"
#include "cppl/obligations/trust.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/vir/module.hpp"
#include "target.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
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

// What the bridge says a diagnostic reports. Only Clang's own diagnostics are
// C++ errors; a refusal of C++ that Clang accepted keeps the category the
// bridge gave it (ARCHITECTURE.md 74, 75).
diagnostics::Category convert(clangbridge::Category category) {
    switch (category) {
        case clangbridge::Category::CppSemantic:
            return diagnostics::Category::CppSemantic;
        case clangbridge::Category::UnsupportedSemantics:
            return diagnostics::Category::UnsupportedSemantics;
        case clangbridge::Category::Elaboration:
            return diagnostics::Category::Elaboration;
        case clangbridge::Category::Internal:
            return diagnostics::Category::Internal;
    }
    return diagnostics::Category::Internal;
}

// A parenthesized `a -> b` with a name on each side is C++ member access in a
// specification too (GRAMMAR.md 29, SPEC.md FORALL-002), which Clang refuses
// where `a` is not a pointer. Where an implication was meant, the diagnostic
// says how to write one there.
void explain_member_reference(diagnostics::Diagnostic& converted) {
    if (converted.category == diagnostics::Category::CppSemantic &&
        converted.message.starts_with("member reference type '") && converted.message.ends_with("' is not a pointer")) {
        converted.notes.push_back(diagnostics::Note{
            "in a contract or a loop invariant, a parenthesized 'a -> b' with a name on each side keeps its C++ "
            "meaning, member access (GRAMMAR.md 29); an implication there is written with a comparison on a side, "
            "'(a -> b == true)', or as '!a || b'",
            converted.location});
    }
}

// Where a reason names a construct, as the author can find it.
std::string written_at(const source::SourceLocation& location) {
    return location.file + ":" + std::to_string(location.line) + ":" + std::to_string(location.column);
}

// Warns at each contract proven for partial correctness only (SPEC.md
// CORRECT-001 to CORRECT-003). A build that succeeds would otherwise leave it to
// the trust report alone to say that such a contract holds only if the
// function returns.
//
// The contracts warned about are the claims the trust report lists as partial
// (CORRECT-006), and the reasons named are those `settle_totality` decided
// that from, so the warning and the report cannot disagree. It is a warning:
// it changes no verdict, no status and no exit code. A function whose
// `decreases` asks that it terminate is not warned about, since not being
// total already refuses it (TERMINATION-006).
void warn_partial_contracts(const obligations::Program& program, const vir::Module& module,
                            const obligations::TrustClosure& closure, diagnostics::Engine& engine) {
    for (const obligations::ClaimClosure& claim : closure.claims) {
        if (claim.kind != obligations::ClaimKind::Contract || claim.total) {
            continue;
        }
        const auto contract =
            std::ranges::find_if(program.contracts, [&claim](const obligations::ContractVerification& candidate) {
                return !candidate.imported.has_value() && candidate.symbol == claim.symbol &&
                       candidate.name == claim.subject;
            });
        if (contract == program.contracts.end()) {
            continue;
        }
        const auto function = std::ranges::find(module.functions, contract->function, &vir::Function::id);
        if (function != module.functions.end() && function->contract.has_value() &&
            !function->contract->measures.empty()) {
            continue;
        }

        diagnostics::Diagnostic diagnostic;
        diagnostic.severity = diagnostics::Severity::Warning;
        diagnostic.category = diagnostics::Category::PartialCorrectness;
        diagnostic.location = function != module.functions.end() ? function->range.begin : claim.location;
        std::vector<std::string> reasons;
        for (const source::SourceLocation& loop : contract->unmeasured_loops) {
            reasons.push_back("the loop at " + written_at(loop) +
                              " states no 'decreases', so it is not shown to terminate (SPEC.md CORRECT-002)");
            diagnostic.notes.push_back(diagnostics::Note{"the loop that states no 'decreases'", loop});
        }
        for (const source::SourceLocation& block : contract->unsafe_regions) {
            reasons.push_back("it passes through the unsafe block at " + written_at(block) +
                              ", which need not return (SPEC.md CORRECT-003)");
            diagnostic.notes.push_back(diagnostics::Note{"the unsafe block, which need not return", block});
        }
        for (const std::size_t callee : contract->partial_callees) {
            if (callee < program.contracts.size()) {
                reasons.push_back("it calls '" + program.contracts[callee].name +
                                  "', whose termination is not established, so the call is not shown to return "
                                  "(SPEC.md CORRECT-005)");
            }
        }
        if (reasons.empty()) {
            reasons.emplace_back("its termination is not established (SPEC.md CORRECT-003)");
        }
        diagnostic.message = "the contract of '" + claim.subject + "' is proven for partial correctness only: ";
        for (std::size_t index = 0; index < reasons.size(); ++index) {
            diagnostic.message += (index == 0 ? "" : "; ") + reasons[index];
        }
        engine.report(std::move(diagnostic));
    }
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
    outcome.files = stream.files();

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
                converted.category = convert(diagnostic.category);
                converted.message = diagnostic.message;
                converted.location = diagnostic.location;
                explain_member_reference(converted);
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

    // Every declaration the projector generates is named with this prefix, and
    // the body lowering reads a declaration so named as one it generated: a
    // loop clause, a claim's evidence, an unsafe or a ghost marker, never code
    // that runs. A name the author wrote with it would be read the same way, so
    // it is refused; C++ reserves such names for the implementation anyway.
    for (const frontend::Token& token : stream.tokens()) {
        if (token.kind == frontend::TokenKind::Identifier &&
            token.text.starts_with(projection_options.generated_prefix)) {
            report(engine, diagnostics::Category::CpplSyntax,
                   "'" + std::string(token.text) + "' begins with '" + projection_options.generated_prefix +
                       "', which names only what C++L generates",
                   stream.location_of(token), "rename it; a name containing '__' is reserved for the implementation");
        }
    }
    if (engine.has_errors()) {
        outcome.tokens = std::make_unique<frontend::TokenStream>(stream);
        outcome.syntax = std::make_unique<frontend::Syntax>(std::move(syntax));
        outcome.failed = true;
        return outcome;
    }

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

    // The driver's own program edits the arguments it compiles with by
    // CCC_OVERRIDE_OPTIONS before it reads them, and libclang never reads it,
    // so under it the analysis would be made under other options than the
    // program is compiled with (TRUST.md TCB-CLANG-006, SPEC.md ARITH-014).
    if (const std::optional<std::string> variable = argument_editing_environment()) {
        report(engine, diagnostics::Category::UnsupportedSemantics,
               "'" + request.original_path + "' is not verified: the environment variable " + *variable +
                   " is set, and the Clang driver edits the arguments it compiles the program with by it",
               {},
               "libclang, which makes the analysis a proof is about, does not read it; unset it, or write the "
               "options it adds on the command line");
        outcome.tokens = std::make_unique<frontend::TokenStream>(stream);
        outcome.syntax = std::make_unique<frontend::Syntax>(std::move(syntax));
        outcome.failed = true;
        return outcome;
    }

    // The analysis is made for the target the program is compiled for, which
    // the driver decides and may decide from more than the arguments say: the
    // prefix of its own name or a configuration file. Every width, layout and
    // conversion a proof rests on is that target's (TRUST.md TCB-CLANG-006,
    // SPEC.md ARITH-014).
    const std::expected<CompileTarget, std::string> target = compile_target(request.clang, request.clang_arguments);
    if (!target.has_value()) {
        report(engine, diagnostics::Category::UnsupportedSemantics,
               "'" + request.original_path + "' is not verified: " + target.error(), {},
               "a proof is made for the target the program is compiled for, and that target must be established");
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
    parse_request.arguments = analysis_arguments(request.clang_arguments, *target);
    parse_request.arguments.emplace_back("-x");
    parse_request.arguments.emplace_back("c++-cpp-output");
    parse_request.arguments.emplace_back("-w");
    if (request.imports != nullptr) {
        for (const obligations::ImportedEntry& imported : request.imports->entries) {
            if (!imported.entry.unsafe.empty()) {
                parse_request.selection.unsafe_symbols.push_back(imported.entry.symbol);
            }
        }
    }

    const std::expected<analysis::Result, std::string> analyzed =
        analysis::analyze(stream, syntax, projection_options, parse_request);
    outcome.tokens = std::make_unique<frontend::TokenStream>(stream);
    outcome.syntax = std::make_unique<frontend::Syntax>(syntax);
    if (!analyzed.has_value()) {
        report(engine, diagnostics::Category::Internal, analyzed.error());
        outcome.failed = true;
        return outcome;
    }
    // Named explicitly, the target is still only what the analysis was asked
    // for. What it was made for is what Clang reports, and nothing it resolved
    // is read unless that is the triple the program is compiled for.
    if (analyzed->unit.target != target->effective) {
        report(engine, diagnostics::Category::UnsupportedSemantics,
               "'" + request.original_path + "' is not verified: its C++ semantics were resolved for target '" +
                   analyzed->unit.target + "', and the Clang driver '" + request.clang +
                   "' compiles the program for '" + target->effective + "'",
               {},
               "a proof is made for the target the program is compiled for; a configuration the analysis cannot "
               "be given exactly is refused rather than verified for another machine");
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
        converted.category = convert(diagnostic.category);
        converted.message = diagnostic.message;
        converted.location = diagnostic.location;
        explain_member_reference(converted);
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
    if (request.stop_after_elaboration) {
        return outcome;
    }

    static const obligations::Imports none;
    // The short form of `induction` asks automation for each case; what it
    // proposes is checked by the kernel there and again with the whole proof.
    const obligations::CaseAutomation cases = [](const kernel::Context& context,
                                                 const kernel::Proposition& claim) -> std::optional<kernel::ProofTerm> {
        std::optional<automation::Evidence> evidence = automation::propose(context, claim);
        if (!evidence.has_value())
            return std::nullopt;
        return std::move(evidence->proof);
    };
    const obligations::Program program = obligations::generate(
        elaborated.module, elaborated, engine, request.imports != nullptr ? *request.imports : none, cases);
    if (!engine.has_errors() && program.proofs.size() != syntax.proofs.size()) {
        report(engine, diagnostics::Category::Internal, "not every written proof produced explicit evidence");
    }
    const std::vector<obligations::ObligationResult> results = automation::verify(program, engine);

    // The verdicts in words, for editors. Taken from each verdict as the kernel
    // left it; nothing below reads them.
    outcome.verified = true;
    outcome.obligations.reserve(results.size());
    for (const obligations::ObligationResult& result : results) {
        ObligationRecord record;
        record.origin = result.obligation.origin;
        record.subject = result.obligation.subject;
        record.location = result.obligation.range.begin;
        record.status = result.verdict.status();
        if (!result.verdict.is_proven()) {
            record.reason = result.verdict.reason();
        }
        record.strategy = result.strategy;
        for (const obligations::TrustedPremise& premise : result.verdict.premises()) {
            record.premises.push_back(premise.name);
        }
        if (const obligations::WrittenProof* written = program.proof_for(result.obligation)) {
            record.written_proof = written->name;
        }
        if (result.obligation.origin == obligations::Origin::LawProposition && result.obligation.law.has_value()) {
            for (const vir::Proof& proof : elaborated.module.proofs) {
                if (proof.law == result.obligation.law) {
                    record.naming_proofs.push_back(proof.name);
                }
            }
        }
        record.goal = kernel::describe(result.obligation.goal);
        outcome.obligations.push_back(std::move(record));
    }
    // A trusted law admitting a memory proposition has no obligation, but an
    // editor shows it TRUSTED exactly as the trust report names it.
    for (const obligations::TrustedMemoryAssumption& assumption : program.memory_assumptions) {
        ObligationRecord record;
        record.origin = obligations::Origin::LawProposition;
        record.subject = assumption.name;
        record.location = assumption.location;
        record.status = obligations::Status::Trusted;
        record.strategy = "explicit trusted assumption";
        record.goal = assumption.statement;
        outcome.obligations.push_back(std::move(record));
    }

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
            } else if (result.obligation.origin == obligations::Origin::CallDescent) {
                ++outcome.counters.call_measures_proven;
            } else if (result.obligation.origin == obligations::Origin::OmittedCase) {
                ++outcome.counters.omitted_cases_proven;
            } else if (result.obligation.origin == obligations::Origin::ImpossiblePath) {
                ++outcome.counters.impossible_paths_proven;
            } else if (result.obligation.origin == obligations::Origin::DefinedBehavior) {
                ++outcome.counters.defined_operations_proven;
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
    // A contract built from conditions has no single obligation of its own: it
    // is established when every one of its conditions is proven, and those of
    // its recursion group. It states total correctness only where its
    // termination is established too (SPEC.md CORRECT-006).
    const auto conditions_proven = [&](const obligations::ContractVerification& contract) {
        return std::ranges::all_of(contract.conditions,
                                   [&results](const obligations::VerificationCondition& condition) {
                                       return results[condition.obligation].verdict.is_proven();
                                   });
    };
    for (const obligations::ContractVerification& contract : program.contracts) {
        if (!contract.partial) {
            continue;
        }
        ++declaration_obligations;
        // Another unit's contract was established by its interface, not proven
        // here, so it accounts for its declaration and is counted apart
        // (SPEC.md TUBOUND-006).
        if (contract.imported.has_value()) {
            ++outcome.counters.contracts_imported;
            continue;
        }
        if (conditions_proven(contract) && std::ranges::all_of(contract.recursion, [&](std::size_t member) {
                return conditions_proven(program.contracts[member]);
            })) {
            ++outcome.counters.contracts_proven;
            if (!contract.total) {
                ++outcome.counters.partial_contracts_proven;
            }
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
    // What each claim rests on, for an editor to name beside its verdict as the
    // trust report names it, including what reaches it only through a proof it
    // uses, a verified function it calls or another unit's contract (TRUST.md
    // TCB-REPORT-002, TCB-REPORT-004, TCB-REPORT-005). A claim's location is its
    // anchoring obligation's, which may be one of its paths.
    const auto add = [](std::vector<std::string>& to, std::string value) {
        if (std::ranges::find(to, value) == to.end()) {
            to.push_back(std::move(value));
        }
    };
    const auto at = [](const std::string& file, std::uint32_t line) {
        return file + ":" + std::to_string(line);
    };
    const auto model = [](std::string name) {
        constexpr std::string_view suffix = " model";
        if (name.ends_with(suffix)) {
            name.resize(name.size() - suffix.size());
        }
        return name;
    };
    for (const obligations::ClaimClosure& claim : outcome.counters.closure.claims) {
        for (ObligationRecord& record : outcome.obligations) {
            if (!(record.location == claim.location)) {
                continue;
            }
            for (const obligations::TrustedPremise& premise : claim.premises) {
                add(record.premises, premise.name);
            }
            for (const obligations::LibraryDependency& library : claim.library) {
                add(record.models, std::string(source::describe_model(library.model)));
            }
            for (const obligations::UnsafeDependency& block : claim.unsafe) {
                add(record.unsafe, at(block.location.file, block.location.line));
            }
            for (const obligations::RuntimeCheck& site : claim.runtime) {
                add(record.validations, site.refinement + " at " + at(site.location.file, site.location.line));
            }
            for (const obligations::ImportedDependency& imported : claim.imported) {
                record.imported.push_back(ImportedRecord{imported.name, imported.origin});
                for (const artifact::Premise& premise : imported.premises) {
                    add(record.premises, premise.name);
                }
                for (const artifact::Model& recorded : imported.models) {
                    add(record.models, model(recorded.name));
                }
                for (const artifact::UnsafeBlock& block : imported.unsafe) {
                    add(record.unsafe, at(block.file, block.line));
                }
                for (const artifact::RuntimeCheck& site : imported.runtime) {
                    add(record.validations, site.refinement + " at " + at(site.file, site.line));
                }
            }
        }
    }
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
    for (const frontend::UnsafeBlock& block : syntax.unsafe_blocks) {
        if (block.nested) {
            continue; // part of the region that holds it
        }
        outcome.counters.unsafe_boundaries.push_back(PipelineOutcome::Counters::UnsafeBoundary{
            block.location,
            block.function_index.has_value() && *block.function_index < syntax.verified_functions.size()
                ? syntax.verified_functions[*block.function_index].function_name
                : std::string{},
            {}});
    }
    for (const std::size_t index : elaborated.unsafe_functions) {
        const frontend::UnsafeFunction& function = syntax.unsafe_functions[index];
        outcome.counters.unsafe_boundaries.push_back(
            PipelineOutcome::Counters::UnsafeBoundary{function.function_location, {}, function.function_name});
    }
    std::ranges::stable_sort(
        outcome.counters.unsafe_boundaries, {}, [](const PipelineOutcome::Counters::UnsafeBoundary& boundary) {
            return std::tie(boundary.location.file, boundary.location.line, boundary.location.column);
        });
    // A contract that rests on an unsafe block this report cannot name would
    // be reported resting on less than it does (TRUST.md TCB-REPORT-005).
    for (const obligations::ClaimClosure& claim : outcome.counters.closure.claims) {
        for (const obligations::UnsafeDependency& dependency : claim.unsafe) {
            const bool named =
                std::ranges::any_of(outcome.counters.unsafe_boundaries,
                                    [&dependency](const PipelineOutcome::Counters::UnsafeBoundary& known) {
                                        return known.function.empty() && known.location == dependency.location;
                                    });
            if (!named) {
                outcome.counters.closure.faults.push_back("the contract of '" + claim.subject +
                                                          "' rests on an unsafe block this unit does not declare");
            }
        }
    }
    for (const std::string& fault : outcome.counters.closure.faults) {
        report(engine, diagnostics::Category::Internal, "what a claim rests on cannot be reported: " + fault);
    }
    warn_partial_contracts(program, elaborated.module, outcome.counters.closure, engine);
    // What an interface of this unit would record. The driver writes it only
    // once the whole unit, and its object, were produced without error.
    outcome.exported = obligations::exported_contracts(program, outcome.counters.closure);

    // A trusted law admitting a memory proposition is accounted for by its
    // recorded assumption rather than by an obligation (SPEC.md TRUSTED-003).
    declaration_obligations += program.memory_assumptions.size();
    // A verified declaration restating a function's contract is accounted for
    // by that function's one contract (SPEC.md TU-003).
    declaration_obligations += elaborated.redeclarations;
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
    //
    // It is named after the source's own stem, in a directory of its own, so an
    // output Clang names after its input when no `-o` names one, such as the
    // object of `-c`, is named as the source's would be: `main.o`, not
    // `main.cpp.runtime.o`.
    const std::filesystem::path runtime_path =
        request.scratch / (std::filesystem::path(request.stem).stem().string() + ".ii");
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
