#include "cppl/driver/driver.hpp"

#include "cppl/automation/evidence.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/crash.hpp"
#include "cppl/driver/options.hpp"
#include "cppl/driver/process.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/erasure/erase.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/kernel/version.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/source/digest.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <cstdint>
#include <random>
#else
#include <unistd.h>
#endif

namespace cppl::driver {

namespace {

struct Summary {
    std::size_t laws = 0;
    std::size_t proven = 0;
    std::size_t contracts_proven = 0;
    std::size_t partial_contracts_proven = 0;
    std::size_t loop_invariants_proven = 0;
    std::size_t call_preconditions_proven = 0;
    std::size_t proven_by_written_proof = 0;
    std::size_t proofs_proven = 0;
    std::size_t unresolved = 0;
    std::size_t units_verified = 0;
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

class ScratchDirectory {
  public:
    ScratchDirectory() {
        std::error_code error;
        const auto temporary = std::filesystem::temp_directory_path(error);
        if (error) {
            return;
        }
#ifdef _WIN32
        // The per-user temporary directory is already private, so a name no
        // other process holds is enough. create_directory reports false without
        // an error when the name is taken, which is the collision to retry.
        std::mt19937_64 generator{std::random_device{}()};
        for (int attempt = 0; attempt < 64; ++attempt) {
            std::string name = "cppl-";
            const std::uint64_t value = generator();
            for (int shift = 60; shift >= 0; shift -= 4) {
                name.push_back("0123456789abcdef"[(value >> shift) & 0xF]);
            }
            const std::filesystem::path candidate = temporary / name;
            std::error_code creation;
            if (std::filesystem::create_directory(candidate, creation)) {
                path_ = candidate;
                return;
            }
            if (creation) {
                return;
            }
        }
#else
        // mkdtemp creates the directory owner-only, so the preprocessed source
        // and the projections are not exposed in a shared temporary directory.
        std::string pattern = (temporary / "cppl-XXXXXX").string();
        if (const char* created = ::mkdtemp(pattern.data())) {
            path_ = created;
        }
#endif
    }

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    ~ScratchDirectory() {
        if (!path_.empty()) {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

std::filesystem::path scratch_directory(const std::filesystem::path& root, const std::string& input) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(input, error);
    const source::Digest digest = source::hash_bytes(absolute.string());
    const std::filesystem::path directory = root / digest.to_short_hex(16);
    std::filesystem::create_directories(directory, error);
    return error ? std::filesystem::path{} : directory;
}

bool write_file(const std::filesystem::path& path, std::string_view content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(stream);
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
// Everything that affects C++ meaning is kept (ARCHITECTURE.md 51).
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
    if (!preprocessing.started) {
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
    const frontend::TokenStream stream = frontend::lex(*text, input.path);
    const frontend::Syntax syntax = frontend::recognize(stream, engine);

    if (engine.has_errors()) {
        outcome.failed = true;
        return outcome;
    }

    if (syntax.empty()) {
        // Ordinary C++: nothing to verify, and the original file is what gets
        // compiled (ARCHITECTURE.md 29).
        return outcome;
    }

    outcome.has_cppl = true;
    ++summary.units_verified;

    if (input.is_header) {
        report(engine, diagnostics::Category::UnsupportedSemantics,
               "'" + input.path + "' contains C++L constructs and is being compiled directly", {},
               "include the header from a source file so its constructs are verified there");
        outcome.failed = true;
        return outcome;
    }
    if (options.explicit_language) {
        report(engine, diagnostics::Category::UnsupportedSemantics,
               "'-x' is not supported together with C++L constructs", {},
               "this implementation selects the input language itself when it projects a unit");
        outcome.failed = true;
        return outcome;
    }

    frontend::ProjectionOptions projection_options;
    projection_options.unit_key = source::hash_bytes(std::filesystem::absolute(input.path).string()).to_short_hex(12);
    const Stage projecting_stage{"projecting"};
    const frontend::Projection projection = frontend::project(stream, syntax, projection_options);
    for (const auto& diagnostic : projection.diagnostics)
        engine.report(diagnostic);
    if (engine.has_errors()) {
        outcome.failed = true;
        return outcome;
    }

    const std::filesystem::path analysis_path = scratch / (stem + ".analysis.cpp");
    const std::filesystem::path runtime_path = scratch / (stem + ".runtime.cpp");
    if (!write_file(analysis_path, projection.analysis) || !write_file(runtime_path, projection.runtime)) {
        report(engine, diagnostics::Category::Internal, "could not write the projection of '" + input.path + "'");
        outcome.failed = true;
        return outcome;
    }

    clangbridge::ParseRequest request;
    request.path = analysis_path.string();
    request.arguments = base_arguments(options);
    request.arguments.emplace_back("-x");
    request.arguments.emplace_back("c++-cpp-output");
    request.arguments.emplace_back("-w");
    request.selection.specification_prefix = projection_options.generated_prefix;
    for (const auto& proposition : projection.proposition_probes) {
        request.selection.proposition_probes.push_back({proposition.name, proposition.shape});
    }
    // A refinement type resolves to its base type like any other alias, so the
    // bridge is told which names carry a predicate (SPEC.md 17).
    for (const auto& refinement : projection.refinement_probes) {
        // A predicate that is an ordinary C++ expression is read from the probe's
        // body, exactly as a law's proposition is; one that states formal syntax
        // is read from its recorded shape.
        if (refinement.shape.kind != source::ProjectionKind::Expression) {
            request.selection.proposition_probes.push_back({refinement.probe, refinement.shape});
        }
        request.selection.refinements.push_back({refinement.name, refinement.probe, refinement.index_count});
    }
    for (const auto& declaration : projection.declaration_offsets) {
        request.selection.offsets.push_back(declaration.analysis);
    }
    for (const auto& law : projection.specification_functions) {
        request.selection.offsets.push_back(law.analysis_offset);
    }

    const Stage parsing_stage{"parsing the analysis projection"};
    const std::expected<clangbridge::TranslationUnit, std::string> unit = clangbridge::parse(request);
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
        engine.report(std::move(converted));
    }
    if (unit->has_errors) {
        outcome.failed = true;
        return outcome;
    }

    const Stage elaborating_stage{"elaborating"};
    const elaboration::Result elaborated =
        elaboration::elaborate(elaboration::Request{syntax, projection, *unit}, engine);

    const Stage generating_stage{"generating the proof obligations"};
    const obligations::Program program = obligations::generate(elaborated.module, elaborated, engine);
    if (!engine.has_errors() && program.proofs.size() != syntax.proofs.size()) {
        report(engine, diagnostics::Category::Internal, "not every written proof produced explicit evidence");
    }
    const Stage verifying_stage{"verifying the proof obligations"};
    const std::vector<obligations::ObligationResult> results = automation::verify(program, engine);

    summary.laws += elaborated.module.laws.size();
    std::size_t declaration_obligations = 0;
    for (const obligations::ObligationResult& result : results) {
        if (result.obligation.origin == obligations::Origin::LawProposition ||
            result.obligation.origin == obligations::Origin::FunctionContract) {
            ++declaration_obligations;
        }
        if (result.verdict.is_proven()) {
            if (result.obligation.origin == obligations::Origin::FunctionContract) {
                ++summary.contracts_proven;
            } else if (result.obligation.origin == obligations::Origin::CallPrecondition) {
                ++summary.call_preconditions_proven;
            } else if (result.obligation.origin == obligations::Origin::LawProposition) {
                ++summary.proven;
            } else if (result.obligation.origin == obligations::Origin::ProofProposition) {
                ++summary.proofs_proven;
            } else if (result.obligation.origin == obligations::Origin::LoopEntry ||
                       result.obligation.origin == obligations::Origin::LoopPreservation) {
                ++summary.loop_invariants_proven;
            }
            if (result.obligation.law && program.proof_for(result.obligation) != nullptr) {
                ++summary.proven_by_written_proof;
            }
        } else {
            ++summary.unresolved;
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
            ++summary.contracts_proven;
            ++summary.partial_contracts_proven;
        }
    }
    const std::size_t required = syntax.laws.size() + syntax.verified_functions.size();
    if (declaration_obligations < required) {
        summary.unresolved += required - declaration_obligations;
        if (!engine.has_errors()) {
            report(engine, diagnostics::Category::Internal,
                   "not every formal declaration produced a verification obligation");
        }
    }

    const Stage erasing_stage{"erasing the proof-only text"};
    const erasure::Erased erased = erasure::erase(stream, syntax, projection, engine);
    if (!erased.report.only_deletions || !erased.report.lines_preserved) {
        outcome.failed = true;
        return outcome;
    }

    if (engine.has_errors()) {
        outcome.failed = true;
        return outcome;
    }

    if (!options.emit_projection.empty() && !write_file(options.emit_projection, erased.runtime)) {
        report(engine, diagnostics::Category::Internal,
               "could not write the runtime projection to '" + options.emit_projection + "'");
        outcome.failed = true;
        return outcome;
    }

    outcome.runtime_path = runtime_path.string();
    return outcome;
}

void print_diagnostics(const diagnostics::Engine& engine) {
    for (const diagnostics::Diagnostic& diagnostic : engine.diagnostics()) {
        std::cerr << diagnostics::render(diagnostic) << "\n";
    }
}

void print_trust_report(const Options& options, const Summary& summary) {
    std::cout << "C++L Trust Report\n\n";
    std::cout << "Laws proven:                 " << summary.proven << "\n";
    std::cout << "  by a written proof:        " << summary.proven_by_written_proof << "\n";
    std::cout << "Proof declarations proven:   " << summary.proofs_proven << "\n";
    std::cout << "Laws trusted:                0\n";
    std::cout << "Function contracts proven:   " << summary.contracts_proven << "\n";
    std::cout << "  partial correctness only:  " << summary.partial_contracts_proven << "\n";
    std::cout << "Call preconditions proven:   " << summary.call_preconditions_proven << "\n";
    std::cout << "Loop invariants proven:      " << summary.loop_invariants_proven << "\n";
    std::cout << "Unresolved obligations:      " << summary.unresolved << "\n\n";
    std::cout << "Unsafe regions:              0\n";
    std::cout << "Runtime validation sites:    0\n";
    std::cout << "Unverified FFI boundaries:   not analysed\n\n";
    std::cout << "Trusted solvers:             0\n";
    std::cout << "Trusted external axioms:     0\n\n";
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
        if (!result.started) {
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
    if (!result.started) {
        std::cerr << "cppl: error: " << result.error << "\n";
        return 1;
    }
    return result.exit_code;
}

} // namespace cppl::driver
