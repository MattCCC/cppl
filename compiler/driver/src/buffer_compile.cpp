#include "cppl/driver/buffer_compile.hpp"

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/process.hpp"
#include "cppl/driver/scratch.hpp"
#include "pipeline.hpp"

#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cppl::driver {

namespace {

void report_internal(diagnostics::Engine& engine, std::string message) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = diagnostics::Category::Internal;
    diagnostic.message = std::move(message);
    engine.report(std::move(diagnostic));
}

std::optional<std::string> read_scratch_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// A `#line` naming the buffer by the document's own path, so every location the
// compile reports names the document rather than the scratch copy Clang reads.
// Quoted includes still resolve against the copy's directory, where they
// resolved before. Empty when the path cannot be spelled in a line directive.
std::string naming_line(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::string directive = "#line 1 \"";
    for (const char character : path) {
        if (character == '\n' || character == '\r') {
            return {};
        }
        if (character == '\\' || character == '"') {
            directive.push_back('\\');
        }
        directive.push_back(character);
    }
    directive += "\"\n";
    return directive;
}

} // namespace

BufferCompileOutcome compile_buffer(const BufferCompileRequest& request, diagnostics::Engine& engine) {
    BufferCompileOutcome outcome;

    // An isolated scratch directory per call. A buffer compile is not shared
    // across calls the way the CLI's per-input scratch subdirectory is (the
    // LSP recompiles the whole buffer on every meaningful edit), so a fresh
    // temporary directory that is removed when this call returns is simpler
    // and just as safe.
    const ScratchDirectory scratch;
    if (scratch.path().empty()) {
        report_internal(engine,
                        "could not create an isolated compilation directory for '" + request.virtual_path + "'");
        outcome.ok = false;
        return outcome;
    }

    // The virtual path's own filename, so scratch file names stay readable
    // and stable across calls for the same document.
    const std::filesystem::path virtual_path(request.virtual_path);
    std::string stem = virtual_path.filename().string();
    if (stem.empty()) {
        stem = "buffer";
    }

    // The buffer is written under the virtual path's own basename, in the
    // scratch directory, so file names stay readable.
    const std::filesystem::path source_path = scratch.path() / stem;
    if (!write_scratch_file(source_path, naming_line(request.virtual_path) + request.text)) {
        report_internal(engine, "could not write a scratch copy of '" + request.virtual_path + "'");
        outcome.ok = false;
        return outcome;
    }

    const std::filesystem::path preprocessed_path = scratch.path() / (stem + ".i");

    // A quoted `#include` is looked for first beside the file that writes it.
    // The copy Clang reads is not there, so the document's own directory is
    // searched for quoted includes ahead of any the flags name, as it would be.
    std::vector<std::string> preprocess;
    if (const std::filesystem::path directory = virtual_path.parent_path(); virtual_path.is_absolute()) {
        preprocess.emplace_back("-iquote");
        preprocess.push_back(directory.string());
    }
    preprocess.insert(preprocess.end(), request.clang_arguments.begin(), request.clang_arguments.end());
    preprocess.emplace_back("-E");
    preprocess.push_back(source_path.string());
    preprocess.emplace_back("-o");
    preprocess.push_back(preprocessed_path.string());
    preprocess.emplace_back("-w");

    const std::string clang = request.clang.empty() ? std::string{CPPL_DEFAULT_CLANG} : request.clang;
    const ProcessResult preprocessing = run(clang, preprocess);
    if (!preprocessing.started || preprocessing.signaled) {
        report_internal(engine, "could not run the C++ preprocessor for '" + request.virtual_path +
                                    "': " + preprocessing.error);
        outcome.ok = false;
        return outcome;
    }
    if (preprocessing.exit_code != 0) {
        // Preprocessing itself failed: a malformed #include, an unknown
        // flag, or similar. Clang already wrote the reason to stderr (the
        // LSP process's stderr, never the JSON-RPC stdout channel); report a
        // diagnostic here too so a caller that only looks at the engine
        // still learns something failed, rather than seeing an empty,
        // silently successful result.
        report_internal(engine, "preprocessing '" + request.virtual_path + "' failed (clang exit code " +
                                    std::to_string(preprocessing.exit_code) + ")");
        outcome.ok = false;
        return outcome;
    }

    std::optional<std::string> text = read_scratch_file(preprocessed_path);
    if (!text.has_value()) {
        report_internal(engine, "could not read the preprocessed form of '" + request.virtual_path + "'");
        outcome.ok = false;
        return outcome;
    }
    // The pipeline recognizes the text in place, and the tokens and syntax it
    // returns refer into it, so it lives as long as the outcome that carries
    // them rather than as long as this call.
    outcome.text = std::make_unique<const std::string>(std::move(*text));

    detail::PipelineRequest pipeline_request;
    pipeline_request.preprocessed_text = *outcome.text;
    pipeline_request.original_path = request.virtual_path;
    pipeline_request.original_text = request.text;
    pipeline_request.scratch = scratch.path();
    pipeline_request.stem = stem;
    pipeline_request.clang = clang;
    pipeline_request.clang_arguments = request.clang_arguments;
    pipeline_request.stop_after_elaboration = request.stop_after_elaboration;
    // The LSP has no later "real compile" step of its own, unlike the CLI,
    // so a document with no C++L syntax at all still needs Clang's own
    // diagnostics to reach the editor (README.md: "Ordinary C++ remains
    // ordinary C++").
    pipeline_request.check_ordinary_cpp_with_clang = true;
    // The LSP never asks for the runtime projection to be written anywhere
    // observable: it only wants diagnostics.

    detail::PipelineOutcome result = detail::run_pipeline(pipeline_request, engine);
    outcome.has_cppl = result.has_cppl;
    outcome.tokens = std::move(result.tokens);
    outcome.syntax = std::move(result.syntax);
    outcome.subject_states = std::move(result.subject_states);
    outcome.names = std::move(result.names);
    outcome.verified = result.verified;
    outcome.obligations = std::move(result.obligations);
    return outcome;
}

} // namespace cppl::driver
