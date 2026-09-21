#include "cppl/driver/driver.hpp"

#include "cppl/clang/bridge.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/crash.hpp"
#include "cppl/driver/options.hpp"
#include "cppl/driver/process.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/kernel/version.hpp"
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
#include <vector>

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
    summary.call_preconditions_proven += result.counters.call_preconditions_proven;
    summary.proven_by_written_proof += result.counters.proven_by_written_proof;
    summary.proofs_proven += result.counters.proofs_proven;
    summary.unresolved += result.counters.unresolved;

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
