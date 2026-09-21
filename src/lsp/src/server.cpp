#include "cppl/lsp/server.hpp"

#include "cppl/driver/buffer_compile.hpp"
#include "cppl/lsp/position.hpp"

#include <utility>

namespace cppl::lsp {

Server::Server(std::string clang, std::vector<std::string> clang_arguments)
    : clang_(std::move(clang)),
      clang_arguments_(std::move(clang_arguments)) {}

void Server::initialize() {
    // Server is now initialized
    // In a real implementation, this would process InitializeParams
}

void Server::initialized() {
    // Client has confirmed initialization
}

void Server::shutdown() {
    shutting_down_ = true;
}

void Server::exit() {
    should_exit_ = true;
}

void Server::text_document_did_open(const TextDocumentItem& item) {
    documents_.open(item);

    // Publish diagnostics for the newly opened document
    if (const auto* doc = documents_.get(item.uri)) {
        publish_diagnostics(*doc);
    }
}

void Server::text_document_did_change(const VersionedTextDocumentIdentifier& id,
                                      const std::vector<TextDocumentContentChangeEvent>& changes) {
    documents_.change(id, changes);

    // Publish updated diagnostics
    if (const auto* doc = documents_.get(id.uri)) {
        publish_diagnostics(*doc);
    }
}

void Server::text_document_did_close(const TextDocumentIdentifier& id) {
    // Publish empty diagnostics to clear them
    if (diagnostic_publisher_) {
        diagnostic_publisher_(id.uri, {});
    }

    documents_.close(id);
}

void Server::publish_diagnostics(const Document& doc) {
    if (!diagnostic_publisher_) {
        return;
    }

    PositionMapper mapper(doc.text());

    // Run the real compile pipeline (preprocess -> recognize -> project ->
    // Clang parse -> elaborate -> obligations -> verify -> erase) over the
    // live buffer, exactly as the CLI compiles a file
    // (cppl::driver::compile_buffer, tools/cppl-lsp/README.md). This is the
    // one and only place C++L recognition happens for diagnostics: its
    // tokens/syntax feed the structural Linter below, so the same
    // recognition is never redone (and never double-reported) by this
    // server.
    driver::BufferCompileRequest request;
    request.virtual_path = doc.path();
    request.text = doc.text();
    request.clang = clang_;
    request.clang_arguments = clang_arguments_;

    diagnostics::Engine engine;
    const driver::BufferCompileOutcome outcome = driver::compile_buffer(request, engine);

    std::vector<Diagnostic> lsp_diagnostics;

    if (outcome.tokens && outcome.syntax) {
        // Split the engine's diagnostics: C++L syntax diagnostics (from
        // recognize()) are handed to the structural Linter, which both
        // converts them and adds its own contextual checks over the same
        // syntax tree; every other diagnostic (Clang C++ semantics,
        // elaboration, proof obligations, internal) is converted directly,
        // since the Linter has nothing more to say about them.
        std::vector<diagnostics::Diagnostic> syntax_diagnostics;
        std::vector<diagnostics::Diagnostic> other_diagnostics;
        for (const diagnostics::Diagnostic& diagnostic : engine.diagnostics()) {
            if (diagnostic.category == diagnostics::Category::CpplSyntax) {
                syntax_diagnostics.push_back(diagnostic);
            } else {
                other_diagnostics.push_back(diagnostic);
            }
        }

        lsp_diagnostics = linter_.lint(*outcome.tokens, *outcome.syntax, syntax_diagnostics, mapper);
        for (const diagnostics::Diagnostic& diagnostic : other_diagnostics) {
            lsp_diagnostics.push_back(linter_.convert_diagnostic(diagnostic, mapper));
        }
    } else {
        // The pipeline could not even preprocess the buffer (missing clang,
        // a broken #include, ...): fall back to the document's own
        // structural parse, if it has one, so the editor still sees
        // whatever C++L-aware diagnostics are available, and report every
        // engine diagnostic (preprocessing failures) directly.
        if (doc.tokens() && doc.syntax()) {
            lsp_diagnostics = linter_.lint(*doc.tokens(), *doc.syntax(), doc.diagnostics(), mapper);
        }
        for (const diagnostics::Diagnostic& diagnostic : engine.diagnostics()) {
            lsp_diagnostics.push_back(linter_.convert_diagnostic(diagnostic, mapper));
        }
    }

    diagnostic_publisher_(doc.uri(), std::move(lsp_diagnostics));
}

} // namespace cppl::lsp
