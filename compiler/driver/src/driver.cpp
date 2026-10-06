#include "cppl/driver/driver.hpp"

#include "cppl/artifact/interface.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/crash.hpp"
#include "cppl/driver/options.hpp"
#include "cppl/driver/process.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/driver/trust_report.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/kernel/version.hpp"
#include "cppl/obligations/interface.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/trust.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/verifier_semantics.hpp"
#include "interface_io.hpp"
#include "pipeline.hpp"
#include "version.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cppl::driver {

namespace {

struct UnitOutcome {
    bool failed = false;
    bool has_cppl = false;
    int exit_code = 0;
    std::string runtime_path;
    // What the unit's verification interface records (SPEC.md TUBOUND-002).
    std::vector<std::string> files;
    std::vector<artifact::Entry> exported;
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

// Whether the argument at `index` is the value of the option before it, as
// Clang's driver reads the command line (Options::option_value).
bool option_value(const Options& options, std::size_t index) {
    return index < options.option_value.size() && options.option_value[index];
}

// How many arguments the option at `index` spans with its values.
std::size_t option_span(const Options& options, std::size_t index) {
    std::size_t end = index + 1;
    while (end < options.arguments.size() && option_value(options, end)) {
        ++end;
    }
    return end - index;
}

// The user's command line with the parts that select output and inputs removed,
// so the same configuration can drive preprocessing and semantic analysis.
// Every input goes, whatever it is: another source, an object or a library is
// no part of how one unit reads, and preprocessing it beside the unit would ask
// for an output each. Everything that affects C++ meaning is kept
// (ARCHITECTURE.md 80).
//
// An option goes with every value it has and none it does not, and a value is
// never taken for an option or an input: `-Xclang -c` keeps its `-c`, and
// `--include-directory dir` keeps its `dir`. Either cut otherwise would leave
// an option to take the next argument as its value here and not in the command
// the program is compiled with (SPEC.md ARITH-014).
std::vector<std::string> base_arguments(const Options& options) {
    std::vector<bool> is_input(options.arguments.size(), false);
    for (const std::size_t index : options.positional) {
        if (index < is_input.size()) {
            is_input[index] = true;
        }
    }

    std::vector<std::string> arguments;
    for (std::size_t index = 0; index < options.arguments.size();) {
        const std::string& argument = options.arguments[index];
        const std::size_t span = option_span(options, index);
        const bool removed = argument == "-o" || argument == "-c" || argument == "-S" || is_input[index];
        if (!removed) {
            for (std::size_t kept = index; kept < index + span; ++kept) {
                arguments.push_back(options.arguments[kept]);
            }
        }
        index += span;
    }
    return arguments;
}

// The options that have the preprocessing of `input` write the dependency file
// the build asked for with `-MD` or `-MMD` where Clang would write it for the
// build's own command, naming the target Clang would name. Preprocessing writes
// a scratch file, which Clang would otherwise name as the target and derive the
// dependency file's name from; and the runtime program compiled afterwards is
// already preprocessed, so its compile writes none. Nothing when none was asked
// for.
std::vector<std::string> dependency_output(const Options& options, const Input& input) {
    bool wanted = false;
    bool named_file = false;
    bool named_target = false;
    for (const std::string& argument : options.arguments) {
        wanted = wanted || argument == "-MD" || argument == "-MMD";
        named_file = named_file || argument.starts_with("-MF");
        named_target = named_target || argument.starts_with("-MT") || argument.starts_with("-MQ");
    }
    std::vector<std::string> added;
    if (!wanted) {
        return added;
    }
    const std::filesystem::path given(input.path);
    // As Clang names them: after the output when there is one, else after the
    // input, in the current directory.
    if (!named_file) {
        std::filesystem::path file = options.output.empty()
                                         ? std::filesystem::path(given.stem().string() + ".d")
                                         : std::filesystem::path(options.output).replace_extension(".d");
        added.emplace_back("-MF");
        added.push_back(file.string());
    }
    if (!named_target) {
        std::filesystem::path target =
            options.output.empty() ? given.filename().replace_extension(".o") : std::filesystem::path(options.output);
        added.emplace_back("-MQ");
        added.push_back(target.string());
    }
    return added;
}

// Whether Clang compiles an input of this name without preprocessing it: an
// object, a library, or text already preprocessed. Any other input may be
// preprocessed, and needs the options only preprocessing reads.
bool unpreprocessed_input(std::string_view path) {
    constexpr auto kExtensions = std::to_array<std::string_view>(
        {".o", ".obj", ".a", ".lib", ".so", ".dylib", ".tbd", ".dll", ".ii", ".i", ".mii", ".mi", ".bc", ".ll", ".s"});
    return path.find(".so.") != std::string_view::npos ||
           std::ranges::any_of(kExtensions, [path](std::string_view extension) {
               return path.size() > extension.size() && path.ends_with(extension);
           });
}

// How many arguments, from `index`, an option spans that only preprocessing
// reads, and that a compile of preprocessed text alone reports unused; 0 for
// any other. The search paths and the standard library a link also reads are
// among them only when nothing is linked.
std::size_t preprocessing_only(const std::vector<std::string>& arguments, std::size_t index, bool compile_only) {
    constexpr auto kSeparate = std::to_array<std::string_view>({"-I",
                                                                "-isystem",
                                                                "-iquote",
                                                                "-idirafter",
                                                                "-include",
                                                                "-imacros",
                                                                "-U",
                                                                "-MF",
                                                                "-MT",
                                                                "-MQ",
                                                                "-iprefix",
                                                                "-iwithprefix",
                                                                "-iwithprefixbefore",
                                                                "-isystem-after",
                                                                "-cxx-isystem",
                                                                "-iframework",
                                                                "-iwithsysroot",
                                                                "-iframeworkwithsysroot",
                                                                "-include-pch",
                                                                "-Xpreprocessor"});
    constexpr auto kJoined =
        std::to_array<std::string_view>({"-I", "-U", "-isystem", "-iquote", "-idirafter", "-imacros", "-MF", "-MT",
                                         "-MQ", "-iframework", "--embed-dir=", "-Wp,"});
    constexpr auto kSwitches = std::to_array<std::string_view>({"-MD", "-MMD", "-MP", "-MG", "-MV", "-nostdinc++"});
    constexpr auto kSearchSeparate = std::to_array<std::string_view>({"-isysroot", "-F"});
    constexpr auto kSearchJoined = std::to_array<std::string_view>({"-isysroot", "-F", "-stdlib="});
    const std::string_view argument = arguments[index];
    const auto separate = [&](const auto& options) {
        return std::ranges::find(options, argument) != options.end();
    };
    const auto joined = [&](const auto& options) {
        return std::ranges::any_of(options, [argument](std::string_view option) {
            return argument.size() > option.size() && argument.starts_with(option);
        });
    };
    const std::size_t value = index + 1 < arguments.size() ? 2 : 1;
    if (separate(kSeparate) || (compile_only && separate(kSearchSeparate))) {
        return value;
    }
    if (separate(kSwitches) || joined(kJoined) || (compile_only && joined(kSearchJoined))) {
        return 1;
    }
    return 0;
}

// The command the C++ compiler runs: the build's own, with each verified unit
// replaced by its runtime program. The program is preprocessed text, so the
// language is selected for it and reset for whatever input follows, an object
// or a library as much as a source. When no input is left that is
// preprocessed, the options only preprocessing reads were read when each unit
// was, and are left out rather than reported unused.
std::vector<std::string> compile_arguments(const Options& options,
                                           const std::map<std::size_t, std::string>& replacements) {
    const bool preprocessing =
        options.explicit_language || std::ranges::any_of(options.positional, [&](std::size_t index) {
            return !replacements.contains(index) && !unpreprocessed_input(options.arguments[index]);
        });
    std::vector<std::string> arguments;
    arguments.reserve(options.arguments.size());
    for (std::size_t index = 0; index < options.arguments.size(); ++index) {
        if (!replacements.empty() && !preprocessing) {
            // An option's value stays with its option, whatever it says, and an
            // option left out takes its values with it (SPEC.md ARITH-014).
            if (!option_value(options, index) &&
                preprocessing_only(options.arguments, index, options.compile_only) != 0) {
                index += option_span(options, index) - 1;
                continue;
            }
        }
        const auto replacement = replacements.find(index);
        if (replacement == replacements.end()) {
            arguments.push_back(options.arguments[index]);
            continue;
        }
        const bool more_inputs_follow =
            std::ranges::any_of(options.positional, [index](std::size_t later) { return later > index; });
        arguments.emplace_back("-x");
        arguments.emplace_back("c++-cpp-output");
        arguments.push_back(replacement->second);
        if (more_inputs_follow) {
            arguments.emplace_back("-x");
            arguments.emplace_back("none");
        }
    }
    return arguments;
}

UnitOutcome compile_unit(const Options& options, const Input& input, const std::filesystem::path& scratch_root,
                         const obligations::Imports& imports, diagnostics::Engine& engine, TrustSummary& summary) {
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
    const std::vector<std::string> dependencies = dependency_output(options, input);
    preprocess.insert(preprocess.end(), dependencies.begin(), dependencies.end());
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
    request.imports = &imports;
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
    outcome.files = result.files;
    outcome.exported = result.exported;
    summary.imports.insert(summary.imports.end(), result.counters.closure.imports.begin(),
                           result.counters.closure.imports.end());

    summary.laws += result.counters.laws;
    summary.proven += result.counters.proven;
    summary.contracts_proven += result.counters.contracts_proven;
    summary.partial_contracts_proven += result.counters.partial_contracts_proven;
    summary.loop_invariants_proven += result.counters.loop_invariants_proven;
    summary.loop_measures_proven += result.counters.loop_measures_proven;
    summary.call_measures_proven += result.counters.call_measures_proven;
    summary.omitted_cases_proven += result.counters.omitted_cases_proven;
    summary.impossible_paths_proven += result.counters.impossible_paths_proven;
    summary.call_preconditions_proven += result.counters.call_preconditions_proven;
    summary.defined_operations_proven += result.counters.defined_operations_proven;
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
        summary.trusted.push_back(ReportedAssumption{assumption, used});
    }
    summary.claims.insert(summary.claims.end(), closure.claims.begin(), closure.claims.end());
    summary.runtime_sites.insert(summary.runtime_sites.end(), closure.runtime_sites.begin(),
                                 closure.runtime_sites.end());
    summary.memory_trusted.insert(summary.memory_trusted.end(), closure.memory_assumptions.begin(),
                                  closure.memory_assumptions.end());
    summary.unsafe.insert(summary.unsafe.end(), result.counters.unsafe_boundaries.begin(),
                          result.counters.unsafe_boundaries.end());

    return outcome;
}

// `-w` silences C++L's warnings as it silences Clang's (SPEC.md WORD-018). An
// error is never silenced.
void print_diagnostics(const diagnostics::Engine& engine, const Options& options) {
    const bool silenced = std::ranges::contains(options.arguments, std::string("-w")) ||
                          std::ranges::contains(options.arguments, std::string("--no-warnings"));
    for (const diagnostics::Diagnostic& diagnostic : engine.diagnostics()) {
        if (silenced && diagnostic.severity == diagnostics::Severity::Warning) {
            continue;
        }
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

std::string written_at(const source::SourceLocation& location) {
    return location.file + ":" + std::to_string(location.line) + ":" + std::to_string(location.column);
}

// An imported contract as the report names it: the function, the interface that
// recorded it, and that record's identity.
std::string imported_from(const obligations::ImportedDependency& imported) {
    return "contract of " + imported.name + " [" + imported.symbol + "], imported from " + imported.origin +
           ", entry " + imported.entry.to_short_hex(16);
}

// What another unit's proof of an imported contract rests on, each kind apart,
// and what it was itself proven through, so a claim's closure is complete
// however many units it crosses (SPEC.md TUBOUND-006, TRUST.md TCB-PROV-004).
// Every text is the interface's, so it is shown escaped (TRUST.md TCB-XTU-010).
void print_depends(const obligations::ImportedDependency& imported) {
    for (const artifact::Premise& premise : imported.premises) {
        std::cout << "      whose proof rests on trusted law " << artifact::displayed(premise.name) << " ("
                  << artifact::displayed(premise.file) << ":" << premise.line << "), identity "
                  << premise.identity.to_short_hex(16) << "\n";
    }
    for (const artifact::UnsafeBlock& block : imported.unsafe) {
        std::cout << "      whose proof rests on unsafe block (" << artifact::displayed(block.file) << ":" << block.line
                  << ":" << block.column << ")\n";
    }
    for (const artifact::Model& model : imported.models) {
        std::cout << "      whose proof rests on the " << artifact::displayed(model.name) << "\n";
    }
    for (const artifact::RuntimeCheck& check : imported.runtime) {
        std::cout << "      whose proof rests on the validation of " << artifact::displayed(check.refinement) << " ("
                  << artifact::displayed(check.file) << ":" << check.line << ":" << check.column << ")\n";
    }
    for (const artifact::Dependency& dependency : imported.depends) {
        std::cout << "      which rests on the contract of [" << artifact::displayed(dependency.symbol) << "], entry "
                  << dependency.entry.to_short_hex(16) << "\n";
    }
}

// The proven claims of one kind that rest on nothing, those that rest on at
// least one trusted law, and for claims about runtime code those that rest on
// unsafe code. All are PROVEN; only the first are proven outright (TRUST.md
// 3.2, TCB-REPORT-005).
void print_closure_counts(const TrustSummary& summary, obligations::ClaimKind kind) {
    const auto of_kind = [&summary, kind](auto predicate) {
        return std::ranges::count_if(summary.claims, [kind, &predicate](const obligations::ClaimClosure& claim) {
            return claim.kind == kind && predicate(claim);
        });
    };
    std::cout << "  assumption-free:           " << of_kind(assumption_free) << "\n";
    std::cout << "  relative to trusted laws:  " << of_kind(obligations::rests_on_trusted_laws) << "\n";
    if (kind == obligations::ClaimKind::Contract || kind == obligations::ClaimKind::OmittedCase ||
        kind == obligations::ClaimKind::ImpossiblePath) {
        std::cout << "  relying on unsafe code:    " << of_kind(obligations::rests_on_unsafe_code) << "\n";
        std::cout << "  relying on imported contracts: "
                  << of_kind([](const obligations::ClaimClosure& claim) { return !claim.imported.empty(); }) << "\n";
        std::cout << "  relying on runtime checks: " << of_kind(obligations::rests_on_runtime_checks) << "\n";
    }
}

void print_trust_report(const Options& options, const TrustSummary& summary) {
    std::cout << "C++L Trust Report\n\n";
    std::cout << "Laws proven:                 " << summary.proven << "\n";
    std::cout << "  by a written proof:        " << summary.proven_by_written_proof << "\n";
    print_closure_counts(summary, obligations::ClaimKind::Law);
    std::cout << "Proof declarations proven:   " << summary.proofs_proven << "\n";
    print_closure_counts(summary, obligations::ClaimKind::Proof);
    const std::size_t trusted_laws = summary.trusted.size() + summary.memory_trusted.size();
    std::cout << "Laws trusted:                " << trusted_laws << "\n";
    for (const ReportedAssumption& assumption : summary.trusted) {
        std::cout << "  assumed:                 " << declared_at(assumption.premise) << ", identity "
                  << assumption.premise.identity.text() << "\n";
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
    // Contracts another unit proved, established here only by the interface
    // that recorded them. None is counted as proven above (SPEC.md TUBOUND-006).
    std::cout << "Function contracts imported: " << summary.imports.size() << "\n";
    for (const obligations::ImportedDependency& imported : summary.imports) {
        std::cout << "  imported:                  " << imported_from(imported) << ", "
                  << (imported.total ? "total" : "partial correctness only") << "\n";
        print_depends(imported);
    }
    std::cout << "Call preconditions proven:   " << summary.call_preconditions_proven << "\n";
    std::cout << "Defined operations proven:   " << summary.defined_operations_proven << "\n";
    std::cout << "Loop invariants proven:      " << summary.loop_invariants_proven << "\n";
    std::cout << "Loop measures proven:        " << summary.loop_measures_proven << "\n";
    std::cout << "Recursive call measures proven: " << summary.call_measures_proven << "\n";
    std::cout << "Omitted cases proven:        " << summary.omitted_cases_proven << "\n";
    print_closure_counts(summary, obligations::ClaimKind::OmittedCase);
    std::cout << "Impossible paths proven:     " << summary.impossible_paths_proven << "\n";
    print_closure_counts(summary, obligations::ClaimKind::ImpossiblePath);
    std::cout << "Unresolved obligations:      " << summary.unresolved << "\n\n";

    // Each claim that is proven only relative to trusted laws, with every one
    // of them (TRUST.md TCB-REPORT-002, 36.2), and each trusted law nothing
    // rests on, which an audit can remove without changing any result.
    const auto relative = std::ranges::count_if(summary.claims, obligations::rests_on_trusted_laws);
    std::cout << "Trust-dependent claims:      " << relative << "\n";
    for (const obligations::ClaimClosure& claim : summary.claims) {
        if (!obligations::rests_on_trusted_laws(claim)) {
            continue;
        }
        std::cout << "  " << claim_name(claim) << ", identity " << claim.identity.text() << "\n";
        for (const obligations::TrustedPremise& premise : claim.premises) {
            std::cout << "    rests on " << declared_at(premise) << ", "
                      << (premise.direct ? "named directly" : reached_through(claim.kind)) << "\n";
        }
        // Another unit's trusted laws, which its proof of a contract this claim
        // was proven through rests on (SPEC.md TUBOUND-006, TRUST.md TCB-PROV-004).
        for (const obligations::ImportedDependency& imported : claim.imported) {
            for (const artifact::Premise& premise : imported.premises) {
                std::cout << "    rests on " << artifact::displayed(premise.name) << " ("
                          << artifact::displayed(premise.file) << ":" << premise.line << "), identity "
                          << premise.identity.to_short_hex(16) << ", through the imported " << imported_from(imported)
                          << "\n";
            }
        }
    }
    // Each contract proven with an unsafe block's effects left unknown holds
    // only if that block is sound, which nothing checked, so it stays listed
    // however much else about the function is proven (TRUST.md TCB-REPORT-005).
    const auto reliant = std::ranges::count_if(summary.claims, obligations::rests_on_unsafe_code);
    std::cout << "Unsafe-dependent claims:     " << reliant << "\n";
    for (const obligations::ClaimClosure& claim : summary.claims) {
        if (!obligations::rests_on_unsafe_code(claim)) {
            continue;
        }
        std::cout << "  " << claim_name(claim) << ", identity " << claim.identity.text() << "\n";
        for (const obligations::UnsafeDependency& dependency : claim.unsafe) {
            std::cout << "    rests on unsafe block (" << written_at(dependency.location) << "), "
                      << (dependency.direct ? "in its own body" : "through a verified call it makes") << "\n";
        }
        for (const obligations::ImportedDependency& imported : claim.imported) {
            for (const artifact::UnsafeBlock& block : imported.unsafe) {
                std::cout << "    rests on unsafe block (" << artifact::displayed(block.file) << ":" << block.line
                          << ":" << block.column << "), through the imported " << imported_from(imported) << "\n";
            }
        }
    }
    // Every proven claim is enumerable, not only counted: the ones above rest
    // on trusted laws or unsafe code, and these rest on nothing at all (TRUST.md
    // 36.1, 36.2). One proven through a contract of another unit is never among
    // them; it is listed under Interface-dependent claims (SPEC.md TUBOUND-014).
    std::cout << "Assumption-free claims:      " << std::ranges::count_if(summary.claims, assumption_free) << "\n";
    for (const obligations::ClaimClosure& claim : summary.claims) {
        if (assumption_free(claim)) {
            std::cout << "  " << claim_name(claim) << ", identity " << claim.identity.text() << "\n";
        }
    }
    const auto unused = std::ranges::count_if(summary.trusted, [](const ReportedAssumption& law) { return !law.used; });
    std::cout << "Unused trusted laws:         " << static_cast<std::size_t>(unused) + summary.memory_trusted.size()
              << "\n";
    for (const ReportedAssumption& assumption : summary.trusted) {
        if (!assumption.used) {
            std::cout << "  unused:                  " << declared_at(assumption.premise) << "\n";
        }
    }
    // No statement can use a memory proposition, so each is unused, and the
    // report says why rather than leave an audit to wonder.
    for (const obligations::TrustedMemoryAssumption& assumption : summary.memory_trusted) {
        std::cout << "  unused:                  " << declared_at(assumption)
                  << ", no statement can use a memory proposition\n";
    }
    // Each claim proven with a standard container's operations taken from its
    // model holds only if the library the program runs with behaves as the
    // model states, which nothing checked (SPEC.md STDMODEL-018, TRUST.md
    // 28.1). It is PROVEN relative to that, never assumption-free. A model
    // another unit's proof used arrives with the contract recorded for it,
    // however many units away (TCB-LIB-010, SPEC.md TUBOUND-006).
    const auto library_reliant = std::ranges::count_if(summary.claims, obligations::rests_on_library_models);
    std::cout << "Library-model-dependent claims: " << library_reliant << "\n";
    for (const obligations::ClaimClosure& claim : summary.claims) {
        if (!obligations::rests_on_library_models(claim)) {
            continue;
        }
        std::cout << "  " << claim_name(claim) << ", identity " << claim.identity.text() << "\n";
        for (const obligations::LibraryDependency& dependency : claim.library) {
            std::cout << "    rests on the " << source::describe_model(dependency.model) << " model, "
                      << (dependency.direct ? "in its own contract or body" : "through a verified call it makes")
                      << "\n";
        }
        // The models another unit's proof used: a model is a model wherever the
        // proof that rests on it was made (SPEC.md TUBOUND-006).
        for (const obligations::ImportedDependency& imported : claim.imported) {
            for (const artifact::Model& model : imported.models) {
                std::cout << "    rests on the " << artifact::displayed(model.name) << ", identity "
                          << model.identity.to_short_hex(16) << ", through the imported " << imported_from(imported)
                          << "\n";
            }
        }
    }
    std::cout << "\n";
    // Each contract that holds only if its function returns, named, since a
    // count alone would not say which (SPEC.md CORRECT-006).
    const auto partial = std::ranges::count_if(summary.claims, [](const obligations::ClaimClosure& claim) {
        return claim.kind == obligations::ClaimKind::Contract && !claim.total;
    });
    std::cout << "Partial-correctness contracts: " << partial << "\n";
    for (const obligations::ClaimClosure& claim : summary.claims) {
        if (claim.kind == obligations::ClaimKind::Contract && !claim.total) {
            std::cout << "  " << claim_name(claim) << ", identity " << claim.identity.text() << "\n";
        }
    }
    // Each claim proven through a contract another unit proved holds only if
    // the interface that recorded that proof is faithful to it, which nothing
    // in this unit checked (SPEC.md TUBOUND-006, TRUST.md 31, TCB-XTU-005).
    const auto interfaced = std::ranges::count_if(
        summary.claims, [](const obligations::ClaimClosure& claim) { return !claim.imported.empty(); });
    std::cout << "Interface-dependent claims:  " << interfaced << "\n";
    for (const obligations::ClaimClosure& claim : summary.claims) {
        if (claim.imported.empty()) {
            continue;
        }
        std::cout << "  " << claim_name(claim) << ", identity " << claim.identity.text() << "\n";
        for (const obligations::ImportedDependency& imported : claim.imported) {
            std::cout << "    rests on the " << imported_from(imported) << ", "
                      << (imported.direct ? "called in its own body" : "through a verified call it makes") << "\n";
            print_depends(imported);
        }
    }
    // A verification interface's integrity is checked; where it came from is
    // not. Every imported record is believed on the build's word alone, so no
    // claim resting on one is assumption-free, and nothing in this report says
    // an interface is authentic (SPEC.md TUBOUND-012, TUBOUND-014, TRUST.md
    // TCB-XTU-010).
    if (summary.imports.empty() && summary.interfaces_imported == 0) {
        std::cout << "Interface provenance:        no verification interface was imported\n";
    } else if (summary.imports.empty()) {
        // Imported, and nothing rests on them: their provenance is no less
        // unauthenticated for that, and no claim depends on it.
        const bool one = summary.interfaces_imported == 1;
        std::cout << "Interface provenance:        unauthenticated; " << summary.interfaces_imported
                  << (one ? " imported verification interface" : " imported verification interfaces")
                  << ", and no claim rests on a contract " << (one ? "it records" : "they record")
                  << " (TRUST.md TCB-XTU-010)\n";
    } else {
        std::cout << "Interface provenance:        unauthenticated; " << summary.imports.size()
                  << " imported contracts are believed on the build's word (TRUST.md TCB-XTU-010)\n";
    }
    // Each claim about a body holding a validation expression, or proven
    // through the contract of one, with every such site, its own or a verified
    // callee's. The claim is PROVEN; what it rests on is that the program
    // performs each validation as its lowering states, and what a validation
    // establishes at its site is RUNTIME-CHECKED, never a universal proof
    // (SPEC.md RUNTIMECHECK-012, RUNTIMECHECK-014, TRUST.md TCB-REPORT-004). A
    // validation is not an assumption, so such a claim may also be
    // assumption-free (SPEC.md INTERACT-023). A refinement crossing proven from
    // path facts is proven statically and is no site (RUNTIMECHECK-010).
    const auto checked = std::ranges::count_if(summary.claims, obligations::rests_on_runtime_checks);
    std::cout << "Runtime-check-dependent claims: " << checked << "\n";
    for (const obligations::ClaimClosure& claim : summary.claims) {
        if (!obligations::rests_on_runtime_checks(claim)) {
            continue;
        }
        std::cout << "  " << claim_name(claim) << ", identity " << claim.identity.text() << "\n";
        for (const obligations::RuntimeCheck& check : claim.runtime) {
            std::cout << "    rests on the validation of " << check.refinement << " (" << written_at(check.location)
                      << "), "
                      << (check.direct ? "in its own body"
                                       : "in " + check.function + ", through a verified call it makes")
                      << "\n";
        }
        for (const obligations::ImportedDependency& imported : claim.imported) {
            for (const artifact::RuntimeCheck& check : imported.runtime) {
                std::cout << "    rests on the validation of " << artifact::displayed(check.refinement) << " ("
                          << artifact::displayed(check.file) << ":" << check.line << ":" << check.column
                          << "), through the imported " << imported_from(imported) << "\n";
            }
        }
    }
    // Where the program's guarantees stop, whether or not a proven claim
    // reaches it: an unsafe block outside every verified body still runs.
    std::cout << "Unsafe regions:              " << summary.unsafe.size() << "\n";
    for (const UnsafeBoundary& boundary : summary.unsafe) {
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
    // Every validation expression of a verified body of this unit: what each
    // establishes of the value it tests is RUNTIME-CHECKED, established for
    // that value by executing the test, never proven of every value (SPEC.md
    // RUNTIMECHECK-008, RUNTIMECHECK-014, TRUST.md TCB-REPORT-004).
    std::cout << "Runtime validation sites:    " << summary.runtime_sites.size() << "\n";
    for (const obligations::RuntimeCheck& check : summary.runtime_sites) {
        std::cout << "  RUNTIME-CHECKED:           " << written_at(check.location) << ", validates a value against "
                  << check.refinement << ", where " << check.predicate << ", in verified function " << check.function
                  << "\n";
    }
    std::cout << "Unverified FFI boundaries:   not analysed\n\n";
    std::cout << "Trusted solvers:             0\n";
    std::cout << "Trusted external axioms:     " << trusted_laws << "\n";
    // Every claim above is also relative to the translation from C++ to the
    // core, which is trusted and not verified (TRUST.md 7 to 17, 29).
    std::cout << "Trusted translation:         not verified: C++ semantics as Clang resolves them, the bridge, "
                 "elaboration, obligation construction and the erasure check\n\n";
    std::cout << "Kernel version:              " << kernel::kKernelVersion << "\n";
    std::cout << "Formal core version:         " << kernel::kFormalCoreVersion << "\n";
    std::cout << "Compiler version:            " << CPPL_VERSION << "\n";
    std::cout << "Verification semantics:      " << obligations::kVerificationSemanticsVersion << "\n";
    source::Digest verifier;
    verifier.bytes = kVerifierSemanticsDigest;
    std::cout << "Verifier-semantics digest:   " << verifier.to_hex() << "\n";
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

    // What this compiler's verification results are bound to (SPEC.md
    // TUBOUND-005). `--version` stays Clang's.
    if (options.version) {
        return detail::print_version(options.clang, base_arguments(options), options.standard);
    }

    // An interface records what one unit proved, so it is written for a command
    // that compiles exactly one (SPEC.md TUBOUND-002).
    if (!options.emit_interface.empty() && (options.passthrough || options.inputs.size() != 1)) {
        std::cerr << "cppl: error: '--cppl-emit-interface' records the verification interface of one translation "
                     "unit, and this command compiles "
                  << (options.passthrough ? std::string("none") : std::to_string(options.inputs.size())) << "\n";
        return 1;
    }

    // A trust report says what a compile established, so it is written for a
    // command that compiles something (TRUST.md 36.2).
    if (!options.emit_trust_report.empty() && (options.passthrough || options.inputs.empty())) {
        std::cerr << "cppl: error: '--cppl-emit-trust-report' records what a compile verified, and this command "
                     "compiles nothing\n";
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

    // A unit that is not verified this time leaves no interface or trust
    // report of an earlier compile behind claiming that it was (SPEC.md
    // TUBOUND-005, TRUST.md 36.2).
    const auto fail_with = [&options](int status) {
        if (!options.emit_interface.empty()) {
            detail::withdraw_interface(options.emit_interface);
        }
        if (!options.emit_trust_report.empty()) {
            withdraw_trust_report(options.emit_trust_report);
        }
        return status;
    };

    // The configuration an interface is bound to, needed only when one is
    // written or read (SPEC.md TUBOUND-005), and recorded by a trust report as
    // the build its claims were established by (TRUST.md Annex C.1).
    std::optional<artifact::Configuration> configuration;
    if (!options.emit_interface.empty() || !options.import_interfaces.empty() || !options.emit_trust_report.empty()) {
        std::expected<artifact::Configuration, std::string> current =
            detail::current_configuration(options.clang, base_arguments(options), options.standard);
        if (!current) {
            std::cerr << "cppl: error: " << current.error() << "\n";
            return fail_with(1);
        }
        configuration = std::move(*current);
    }

    diagnostics::Engine engine;
    TrustSummary summary;
    std::map<std::size_t, std::string> replacements;
    int failure_exit_code = 1;
    bool failed = false;

    const obligations::Imports imports = configuration.has_value()
                                             ? detail::read_imports(options.import_interfaces, *configuration, engine)
                                             : obligations::Imports{};
    // Each interface the compile imports, once however often it is named, so
    // a report says so even when no claim rests on what it records.
    std::set<std::filesystem::path> imported_interfaces;
    for (const std::string& path : options.import_interfaces) {
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(path, error);
        imported_interfaces.insert((error ? std::filesystem::path(path) : absolute).lexically_normal());
    }
    summary.interfaces_imported = imported_interfaces.size();

    std::vector<std::string> files;
    std::vector<artifact::Entry> exported;
    for (const Input& input : options.inputs) {
        UnitOutcome outcome = compile_unit(options, input, scratch.path(), imports, engine, summary);
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
        files = std::move(outcome.files);
        exported = std::move(outcome.exported);
    }

    print_diagnostics(engine, options);

    if (failed || engine.has_errors()) {
        return fail_with(failure_exit_code);
    }

    if (options.trust_report) {
        print_trust_report(options, summary);
    }

    // The verified program is the program compiled: the runtime projection is
    // handed to Clang, already preprocessed.
    const std::vector<std::string> arguments = compile_arguments(options, replacements);

    const Stage compiling{"running the C++ compiler"};
    const ProcessResult result = run(options.clang, arguments);
    // A signalled compiler is reported, not impersonated. Returning its
    // `128 + signal` as this process's own status is indistinguishable from
    // `cppl` itself crashing, so the crash report never runs and a build
    // system sees a silent fault with no reason attached (`AGENTS.md` 23).
    if (!result.started || result.signaled) {
        std::cerr << "cppl: error: " << result.error << "\n";
        return fail_with(1);
    }
    if (result.exit_code != 0) {
        return fail_with(result.exit_code);
    }

    // Written only now, once the unit is verified and its object produced, so
    // an interface never describes a build that did not complete (SPEC.md
    // TUBOUND-002).
    if (!options.emit_interface.empty() && configuration.has_value()) {
        artifact::Interface recorded;
        recorded.configuration = *configuration;
        std::error_code error;
        recorded.unit = std::filesystem::absolute(options.inputs.front().path, error).lexically_normal().string();
        // Line markers name every file text was read from but not a resource
        // `#embed` reads, so the interface is bound to the files Clang lists as
        // read as well: an edit to any of them makes it stale (TRUST.md
        // TCB-XTU-008).
        const std::expected<std::vector<std::string>, std::string> read =
            detail::files_read(options.clang, base_arguments(options), options.inputs.front().path,
                               options.inputs.front().is_header, scratch.path());
        if (!read) {
            std::cerr << "cppl: error: cannot record what '" << options.inputs.front().path
                      << "' was verified from: " << read.error() << "\n";
            return fail_with(1);
        }
        files.insert(files.end(), read->begin(), read->end());
        std::expected<std::vector<artifact::SourceFile>, std::string> sources = detail::source_files(files);
        if (error || !sources) {
            std::cerr << "cppl: error: cannot record what '" << options.inputs.front().path
                      << "' was verified from: " << (sources ? error.message() : sources.error()) << "\n";
            return fail_with(1);
        }
        recorded.sources = std::move(*sources);
        recorded.entries = std::move(exported);
        if (const auto written = detail::write_interface(options.emit_interface, recorded); !written) {
            std::cerr << "cppl: error: cannot write verification interface '" << options.emit_interface
                      << "': " << written.error() << "\n";
            return fail_with(1);
        }
    }

    // Written last, once everything it describes exists, so a report is never
    // left describing a compile that did not complete (TRUST.md 36.2).
    if (!options.emit_trust_report.empty() && configuration.has_value()) {
        const std::expected<BuildRecord, std::string> build = detail::build_record(*configuration, options.clang);
        if (!build) {
            std::cerr << "cppl: error: cannot record the build in trust report '" << options.emit_trust_report
                      << "': " << build.error() << "\n";
            return fail_with(1);
        }
        if (const auto written = write_trust_report(options.emit_trust_report, render_trust_report(summary, *build));
            !written) {
            std::cerr << "cppl: error: cannot write trust report '" << options.emit_trust_report
                      << "': " << written.error() << "\n";
            return fail_with(1);
        }
    }
    return 0;
}

} // namespace cppl::driver
