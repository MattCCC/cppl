#include "cppl/driver/driver.hpp"

#include "cppl/clang/bridge.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/crash.hpp"
#include "cppl/driver/options.hpp"
#include "cppl/driver/process.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/kernel/version.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/trust.hpp"
#include "cppl/source/location.hpp"
#include "pipeline.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cppl::driver {

namespace {

struct Summary {
    std::size_t laws = 0;
    std::size_t proven = 0;
    std::size_t contracts_proven = 0;
    std::size_t partial_contracts_proven = 0;
    std::size_t loop_invariants_proven = 0;
    std::size_t loop_measures_proven = 0;
    std::size_t omitted_cases_proven = 0;
    std::size_t impossible_paths_proven = 0;
    std::size_t call_preconditions_proven = 0;
    std::size_t proven_by_written_proof = 0;
    std::size_t proofs_proven = 0;
    std::size_t unresolved = 0;

    // Every explicit assumption, and every proven claim with the trusted laws
    // it rests on, unit by unit in the order the units were given.
    std::vector<obligations::TrustedPremise> trusted;
    std::vector<obligations::ClaimClosure> claims;
    // Trusted laws no proven claim of their own unit rests on.
    std::vector<obligations::TrustedPremise> unused;
    // Trusted laws admitting a memory proposition, which nothing can rest on.
    std::vector<obligations::TrustedMemoryAssumption> memory_trusted;
    // Every unsafe boundary written, whether or not a proven claim rests on it.
    std::vector<detail::PipelineOutcome::Counters::UnsafeBoundary> unsafe;
};

struct UnitOutcome {
    bool failed = false;
    bool has_cppl = false;
    int exit_code = 0;
    std::string runtime_path;
};

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

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// The user's command line with the parts that select output and inputs removed,
// so the same configuration can drive preprocessing and semantic analysis.
// Everything that affects C++ meaning is kept (ARCHITECTURE.md 80).
std::vector<std::string> base_arguments(const Options& options) {
    std::vector<bool> is_input(options.arguments.size(), false);
    for (const Input& input : options.inputs) {
        if (input.argument_index < is_input.size()) {
            is_input[input.argument_index] = true;
        }
    }

    std::vector<std::string> arguments;
    bool skip_value = false;
    for (std::size_t index = 0; index < options.arguments.size(); ++index) {
        const std::string& argument = options.arguments[index];
        if (skip_value) {
            skip_value = false;
            continue;
        }
        if (argument == "-o") {
            skip_value = true;
            continue;
        }
        if (argument == "-c" || argument == "-S" || is_input[index]) {
            continue;
        }
        arguments.push_back(argument);
    }
    return arguments;
}

UnitOutcome compile_unit(const Options& options, const Input& input, const std::filesystem::path& scratch_root,
                         diagnostics::Engine& engine, Summary& summary) {
    UnitOutcome outcome;
    const Stage compiling{"compiling", input.path.c_str()};

    const std::filesystem::path scratch = scratch_directory(scratch_root, input.path);
    if (scratch.empty()) {
        report(engine, diagnostics::Category::Internal,
               "could not create the scratch directory for '" + input.path + "'");
        outcome.failed = true;
        return outcome;
    }
    const std::string stem = std::filesystem::path(input.path).filename().string();
    const std::filesystem::path preprocessed_path = scratch / (stem + ".i");

    // C++L reads the translation unit after preprocessing, so macros are
    // already expanded and constructs written in headers are visible
    // (SPEC.md 3.2).
    std::vector<std::string> preprocess = base_arguments(options);
    preprocess.emplace_back("-E");
    if (input.is_header) {
        preprocess.emplace_back("-x");
        preprocess.emplace_back("c++-header");
    }
    preprocess.push_back(input.path);
    preprocess.emplace_back("-o");
    preprocess.push_back(preprocessed_path.string());
    preprocess.emplace_back("-w");

    const Stage preprocessing_stage{"preprocessing"};
    const ProcessResult preprocessing = run(options.clang, preprocess);
    // A preprocessor that died from a signal wrote no reason to stderr, so
    // unlike an ordinary non-zero status it needs one reported here, and its
    // `128 + signal` must not become this process's own exit status.
    if (!preprocessing.started || preprocessing.signaled) {
        report(engine, diagnostics::Category::Internal, preprocessing.error);
        outcome.failed = true;
        return outcome;
    }
    if (preprocessing.exit_code != 0) {
        // Clang has already reported the reason on stderr.
        outcome.failed = true;
        outcome.exit_code = preprocessing.exit_code;
        return outcome;
    }

    const std::optional<std::string> text = read_file(preprocessed_path);
    if (!text.has_value()) {
        report(engine, diagnostics::Category::Internal, "could not read the preprocessed form of '" + input.path + "'");
        outcome.failed = true;
        return outcome;
    }

    const Stage recognizing_stage{"recognizing the C++L syntax"};
    detail::PipelineRequest request;
    request.preprocessed_text = *text;
    request.original_path = input.path;
    request.scratch = scratch;
    request.stem = stem;
    request.clang = options.clang;
    request.clang_arguments = base_arguments(options);
    request.emit_projection_path = options.emit_projection;
    // A header or an explicit -x cannot be told apart from ordinary C++
    // containing no C++L until recognition has run, which the shared
    // pipeline already does; this hook rejects those two cases from its
    // result instead of recognizing the text a second time.
    const bool is_header = input.is_header;
    const bool explicit_language = options.explicit_language;
    const std::string& input_path = input.path;
    request.reject_if_cppl = [is_header, explicit_language, &input_path](
                                 const frontend::Syntax&) -> std::optional<detail::PipelineRequest::Rejection> {
        if (is_header) {
            return detail::PipelineRequest::Rejection{
                "'" + input_path + "' contains C++L constructs and is being compiled directly",
                "include the header from a source file so its constructs are verified there"};
        }
        if (explicit_language) {
            return detail::PipelineRequest::Rejection{
                "'-x' is not supported together with C++L constructs",
                "this implementation selects the input language itself when it projects a unit"};
        }
        return std::nullopt;
    };

    const detail::PipelineOutcome result = detail::run_pipeline(request, engine);
    outcome.has_cppl = result.has_cppl;
    outcome.runtime_path = result.runtime_path;
    outcome.failed = result.failed;

    summary.laws += result.counters.laws;
    summary.proven += result.counters.proven;
    summary.contracts_proven += result.counters.contracts_proven;
    summary.partial_contracts_proven += result.counters.partial_contracts_proven;
    summary.loop_invariants_proven += result.counters.loop_invariants_proven;
    summary.loop_measures_proven += result.counters.loop_measures_proven;
    summary.omitted_cases_proven += result.counters.omitted_cases_proven;
    summary.impossible_paths_proven += result.counters.impossible_paths_proven;
    summary.call_preconditions_proven += result.counters.call_preconditions_proven;
    summary.proven_by_written_proof += result.counters.proven_by_written_proof;
    summary.proofs_proven += result.counters.proofs_proven;
    summary.unresolved += result.counters.unresolved;

    // A law's identity is only meaningful within the unit that declares it, so
    // whether anything rests on it is decided here, before units are merged.
    const obligations::TrustClosure& closure = result.counters.closure;
    for (const obligations::TrustedPremise& assumption : closure.assumptions) {
        const bool used = std::ranges::any_of(closure.claims, [&assumption](const obligations::ClaimClosure& claim) {
            return std::ranges::contains(claim.premises, assumption.law, &obligations::TrustedPremise::law);
        });
        if (!used) {
            summary.unused.push_back(assumption);
        }
    }
    summary.trusted.insert(summary.trusted.end(), closure.assumptions.begin(), closure.assumptions.end());
    summary.claims.insert(summary.claims.end(), closure.claims.begin(), closure.claims.end());
    summary.memory_trusted.insert(summary.memory_trusted.end(), closure.memory_assumptions.begin(),
                                  closure.memory_assumptions.end());
    summary.unsafe.insert(summary.unsafe.end(), result.counters.unsafe_boundaries.begin(),
                          result.counters.unsafe_boundaries.end());

    return outcome;
}

void print_diagnostics(const diagnostics::Engine& engine) {
    for (const diagnostics::Diagnostic& diagnostic : engine.diagnostics()) {
        std::cerr << diagnostics::render(diagnostic) << "\n";
    }
}

// A trusted law as the report names it: where it is declared, so the
// assumption can be audited there.
std::string declared_at(const obligations::TrustedPremise& premise) {
    return premise.name + " (" + premise.location.file + ":" + std::to_string(premise.location.line) + ")";
}

std::string declared_at(const obligations::TrustedMemoryAssumption& assumption) {
    return assumption.name + " (" + assumption.location.file + ":" + std::to_string(assumption.location.line) + ")";
}

std::string claim_name(const obligations::ClaimClosure& claim) {
    const std::string where = " (" + claim.location.file + ":" + std::to_string(claim.location.line) + ")";
    switch (claim.kind) {
        case obligations::ClaimKind::Law:
            return "law " + claim.subject + where;
        case obligations::ClaimKind::Proof:
            return "proof " + claim.subject + where;
        case obligations::ClaimKind::LawInstance:
            return "proof " + claim.subject + " of a law instance" + where;
        case obligations::ClaimKind::Contract:
            return "contract of " + claim.subject + where;
        case obligations::ClaimKind::OmittedCase:
            return "omitted " + claim.subject + where;
        case obligations::ClaimKind::ImpossiblePath:
            return "unreachable runtime path " + claim.subject + where;
    }
    return claim.subject + where;
}

// How a trusted law reaches a claim that does not name it itself.
std::string reached_through(obligations::ClaimKind kind) {
    switch (kind) {
        case obligations::ClaimKind::OmittedCase:
            return "through the proof it is written in";
        case obligations::ClaimKind::Contract:
            return "through a proof or verified call it uses";
        case obligations::ClaimKind::Law:
        case obligations::ClaimKind::Proof:
        case obligations::ClaimKind::LawInstance:
        case obligations::ClaimKind::ImpossiblePath:
            return "through a proof it uses";
    }
    return "through what it uses";
}

// A claim proven outright: it rests on no trusted law and on no unsafe code.
bool assumption_free(const obligations::ClaimClosure& claim) {
    return claim.premises.empty() && claim.unsafe.empty();
}

std::string written_at(const source::SourceLocation& location) {
    return location.file + ":" + std::to_string(location.line) + ":" + std::to_string(location.column);
}

// The proven claims of one kind that rest on nothing, those that rest on at
// least one trusted law, and for claims about runtime code those that rest on
// unsafe code. All are PROVEN; only the first are proven outright (TRUST.md
// 3.2, TCB-REPORT-005).
void print_closure_counts(const Summary& summary, obligations::ClaimKind kind) {
    const auto of_kind = [&summary, kind](auto predicate) {
        return std::ranges::count_if(summary.claims, [kind, &predicate](const obligations::ClaimClosure& claim) {
            return claim.kind == kind && predicate(claim);
        });
    };
    std::cout << "  assumption-free:           " << of_kind(assumption_free) << "\n";
    std::cout << "  relative to trusted laws:  "
              << of_kind([](const obligations::ClaimClosure& claim) { return !claim.premises.empty(); }) << "\n";
    if (kind == obligations::ClaimKind::Contract || kind == obligations::ClaimKind::OmittedCase ||
        kind == obligations::ClaimKind::ImpossiblePath) {
        std::cout << "  relying on unsafe code:    "
                  << of_kind([](const obligations::ClaimClosure& claim) { return !claim.unsafe.empty(); }) << "\n";
    }
}

void print_trust_report(const Options& options, const Summary& summary) {
    std::cout << "C++L Trust Report\n\n";
    std::cout << "Laws proven:                 " << summary.proven << "\n";
    std::cout << "  by a written proof:        " << summary.proven_by_written_proof << "\n";
    print_closure_counts(summary, obligations::ClaimKind::Law);
    std::cout << "Proof declarations proven:   " << summary.proofs_proven << "\n";
    print_closure_counts(summary, obligations::ClaimKind::Proof);
    const std::size_t trusted_laws = summary.trusted.size() + summary.memory_trusted.size();
    std::cout << "Laws trusted:                " << trusted_laws << "\n";
    for (const obligations::TrustedPremise& assumption : summary.trusted) {
        std::cout << "  assumed:                 " << declared_at(assumption) << ", identity "
                  << assumption.identity.text() << "\n";
    }
    // A memory proposition is assumed like any trusted law, and says what it
    // admits, since that is a capability rather than a proposition.
    for (const obligations::TrustedMemoryAssumption& assumption : summary.memory_trusted) {
        std::cout << "  assumed:                 " << declared_at(assumption) << ", identity "
                  << assumption.identity.text() << ", admits " << assumption.statement << "\n";
    }
    std::cout << "Function contracts proven:   " << summary.contracts_proven << "\n";
    std::cout << "  partial correctness only:  " << summary.partial_contracts_proven << "\n";
    print_closure_counts(summary, obligations::ClaimKind::Contract);
    std::cout << "Call preconditions proven:   " << summary.call_preconditions_proven << "\n";
    std::cout << "Loop invariants proven:      " << summary.loop_invariants_proven << "\n";
    std::cout << "Loop measures proven:        " << summary.loop_measures_proven << "\n";
    std::cout << "Omitted cases proven:        " << summary.omitted_cases_proven << "\n";
    print_closure_counts(summary, obligations::ClaimKind::OmittedCase);
    std::cout << "Impossible paths proven:     " << summary.impossible_paths_proven << "\n";
    print_closure_counts(summary, obligations::ClaimKind::ImpossiblePath);
    std::cout << "Unresolved obligations:      " << summary.unresolved << "\n\n";

    // Each claim that is proven only relative to trusted laws, with every one
    // of them (TRUST.md TCB-REPORT-002, 36.2), and each trusted law nothing
    // rests on, which an audit can remove without changing any result.
    const auto relative = std::ranges::count_if(
        summary.claims, [](const obligations::ClaimClosure& claim) { return !claim.premises.empty(); });
    std::cout << "Trust-dependent claims:      " << relative << "\n";
    for (const obligations::ClaimClosure& claim : summary.claims) {
        if (claim.premises.empty()) {
            continue;
        }
        std::cout << "  " << claim_name(claim) << ", identity " << claim.identity.text() << "\n";
        for (const obligations::TrustedPremise& premise : claim.premises) {
            std::cout << "    rests on " << declared_at(premise) << ", "
                      << (premise.direct ? "named directly" : reached_through(claim.kind)) << "\n";
        }
    }
    // Each contract proven with an unsafe block's effects left unknown holds
    // only if that block is sound, which nothing checked, so it stays listed
    // however much else about the function is proven (TRUST.md TCB-REPORT-005).
    const auto reliant = std::ranges::count_if(
        summary.claims, [](const obligations::ClaimClosure& claim) { return !claim.unsafe.empty(); });
    std::cout << "Unsafe-dependent claims:     " << reliant << "\n";
    for (const obligations::ClaimClosure& claim : summary.claims) {
        if (claim.unsafe.empty()) {
            continue;
        }
        std::cout << "  " << claim_name(claim) << ", identity " << claim.identity.text() << "\n";
        for (const obligations::UnsafeDependency& dependency : claim.unsafe) {
            std::cout << "    rests on unsafe block (" << written_at(dependency.location) << "), "
                      << (dependency.direct ? "in its own body" : "through a verified call it makes") << "\n";
        }
    }
    // Every proven claim is enumerable, not only counted: the ones above rest
    // on trusted laws or unsafe code, and these rest on neither (TRUST.md
    // 36.1, 36.2).
    std::cout << "Assumption-free claims:      " << std::ranges::count_if(summary.claims, assumption_free) << "\n";
    for (const obligations::ClaimClosure& claim : summary.claims) {
        if (assumption_free(claim)) {
            std::cout << "  " << claim_name(claim) << ", identity " << claim.identity.text() << "\n";
        }
    }
    std::cout << "Unused trusted laws:         " << summary.unused.size() + summary.memory_trusted.size() << "\n";
    for (const obligations::TrustedPremise& assumption : summary.unused) {
        std::cout << "  unused:                  " << declared_at(assumption) << "\n";
    }
    // No statement can use a memory proposition, so each is unused, and the
    // report says why rather than leave an audit to wonder.
    for (const obligations::TrustedMemoryAssumption& assumption : summary.memory_trusted) {
        std::cout << "  unused:                  " << declared_at(assumption)
                  << ", no statement can use a memory proposition\n";
    }
    std::cout << "\n";
    // Where the program's guarantees stop, whether or not a proven claim
    // reaches it: an unsafe block outside every verified body still runs.
    std::cout << "Unsafe regions:              " << summary.unsafe.size() << "\n";
    for (const detail::PipelineOutcome::Counters::UnsafeBoundary& boundary : summary.unsafe) {
        if (!boundary.function.empty()) {
            std::cout << "  unsafe function:         " << boundary.function << " (" << written_at(boundary.location)
                      << ")\n";
        } else if (!boundary.owner.empty()) {
            std::cout << "  unsafe block:            " << written_at(boundary.location) << ", in verified function "
                      << boundary.owner << "\n";
        } else {
            std::cout << "  unsafe block:            " << written_at(boundary.location) << "\n";
        }
    }
    std::cout << "Runtime validation sites:    0\n";
    std::cout << "Unverified FFI boundaries:   not analysed\n\n";
    std::cout << "Trusted solvers:             0\n";
    std::cout << "Trusted external axioms:     " << trusted_laws << "\n\n";
    std::cout << "Kernel version:              " << kernel::kKernelVersion << "\n";
    std::cout << "Formal core version:         " << kernel::kFormalCoreVersion << "\n";
    std::cout << "Compiler version:            " << CPPL_VERSION << "\n";
    std::cout << "Clang:                       " << clangbridge::clang_version() << "\n";
    std::cout << "C++ mode:                    " << (options.standard.empty() ? "compiler default" : options.standard)
              << "\n";
}

} // namespace

int run_driver(int argc, const char* const* argv) {
    Options options = parse(argc, argv);

    if (!options.errors.empty()) {
        for (const std::string& error : options.errors) {
            std::cerr << "cppl: error: " << error << "\n";
        }
        return 1;
    }

    if (options.passthrough || options.inputs.empty()) {
        const ProcessResult result = run(options.clang, options.arguments);
        if (!result.started || result.signaled) {
            std::cerr << "cppl: error: " << result.error << "\n";
            return 1;
        }
        return result.exit_code;
    }

    const ScratchDirectory scratch;
    if (scratch.path().empty()) {
        std::cerr << "cppl: error: could not create an isolated compilation directory\n";
        return 1;
    }

    diagnostics::Engine engine;
    Summary summary;
    std::map<std::size_t, std::string> replacements;
    int failure_exit_code = 1;
    bool failed = false;

    for (const Input& input : options.inputs) {
        const UnitOutcome outcome = compile_unit(options, input, scratch.path(), engine, summary);
        if (outcome.failed) {
            failed = true;
            if (outcome.exit_code != 0) {
                failure_exit_code = outcome.exit_code;
            }
            break;
        }
        if (outcome.has_cppl && !outcome.runtime_path.empty()) {
            replacements.emplace(input.argument_index, outcome.runtime_path);
        }
    }

    print_diagnostics(engine);

    if (failed || engine.has_errors()) {
        return failure_exit_code;
    }

    if (options.trust_report) {
        print_trust_report(options, summary);
    }

    std::vector<std::string> arguments;
    arguments.reserve(options.arguments.size());
    for (std::size_t index = 0; index < options.arguments.size(); ++index) {
        const auto replacement = replacements.find(index);
        if (replacement == replacements.end()) {
            arguments.push_back(options.arguments[index]);
            continue;
        }
        // The verified program is the program compiled: the runtime projection
        // is handed to Clang, already preprocessed. The language selection is
        // reset afterwards only when another input follows it, since -x applies
        // to the inputs after it.
        const bool more_inputs_follow =
            std::ranges::any_of(options.inputs, [index](const Input& later) { return later.argument_index > index; });

        arguments.emplace_back("-x");
        arguments.emplace_back("c++-cpp-output");
        arguments.push_back(replacement->second);
        if (more_inputs_follow) {
            arguments.emplace_back("-x");
            arguments.emplace_back("none");
        }
    }

    const Stage compiling{"running the C++ compiler"};
    const ProcessResult result = run(options.clang, arguments);
    // A signalled compiler is reported, not impersonated. Returning its
    // `128 + signal` as this process's own status is indistinguishable from
    // `cppl` itself crashing, so the crash report never runs and a build
    // system sees a silent fault with no reason attached (`AGENTS.md` 23).
    if (!result.started || result.signaled) {
        std::cerr << "cppl: error: " << result.error << "\n";
        return 1;
    }
    return result.exit_code;
}

} // namespace cppl::driver
