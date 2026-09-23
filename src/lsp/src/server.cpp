#include "cppl/lsp/server.hpp"

#include "cppl/clang/editor.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/buffer_compile.hpp"
#include "cppl/formatter/format.hpp"
#include "cppl/lsp/decomposition_view.hpp"
#include "cppl/lsp/document.hpp"
#include "cppl/lsp/editor_view.hpp"
#include "cppl/lsp/hover.hpp"
#include "cppl/lsp/linter.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/proof_names.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/semantic_tokens.hpp"
#include "cppl/lsp/verification.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::lsp {

namespace {

std::vector<TextEdit> to_text_edits(const std::string& text, const std::vector<formatter::FormatEdit>& edits) {
    const PositionMapper mapper(text);
    std::vector<TextEdit> result;
    result.reserve(edits.size());
    for (const formatter::FormatEdit& edit : edits) {
        TextEdit text_edit;
        text_edit.range = mapper.byte_span_to_range(edit.span);
        text_edit.newText = edit.replacement;
        result.push_back(std::move(text_edit));
    }
    return result;
}

// Whether `kind` is among the kinds a request asks for. Kinds are
// hierarchical, so asking for `source` asks for `source.fixAll.cppl` too, and
// asking for none asks for all (LSP: `CodeActionContext.only`).
bool asks_for(const std::vector<std::string>& only, std::string_view kind) {
    if (only.empty()) {
        return true;
    }
    return std::ranges::any_of(only, [&](const std::string& asked) {
        return kind == asked || (kind.starts_with(asked) && kind.size() > asked.size() && kind[asked.size()] == '.');
    });
}

} // namespace

Server::Server(std::string clang, std::vector<std::string> clang_arguments)
    : clang_(std::move(clang)),
      clang_arguments_(std::move(clang_arguments)),
      driver_(resolve_driver(clang_.empty() ? std::string{CPPL_DEFAULT_CLANG} : clang_)) {}

void Server::initialize(ClientCapabilities capabilities) {
    client_ = capabilities;
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
    ++generation_;

    // Publish diagnostics for the newly opened document
    if (const auto* doc = documents_.get(item.uri)) {
        publish_diagnostics(*doc);
    }
}

void Server::text_document_did_change(const VersionedTextDocumentIdentifier& id,
                                      const std::vector<TextDocumentContentChangeEvent>& changes) {
    documents_.change(id, changes);
    ++generation_;

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
    views_.erase(id.uri);
    ++generation_;
}

EditorView* Server::view_for(const std::string& uri) {
    const Document* document = documents_.get(uri);
    if (document == nullptr) {
        return nullptr;
    }
    View& entry = views_[uri];
    if (entry.view == nullptr) {
        entry.view = std::make_unique<EditorView>();
    }
    if (entry.generation != generation_) {
        std::vector<OpenBuffer> open;
        documents_.for_each([&open](const Document& buffer) {
            open.push_back(OpenBuffer{buffer.uri(), buffer.path(), &buffer.text()});
        });
        EditorView::Options options;
        options.driver = driver_;
        options.arguments = clang_arguments_;
        entry.view->refresh(OpenBuffer{document->uri(), document->path(), &document->text()}, open, options);
        entry.generation = generation_;
    }
    return entry.view.get();
}

std::optional<std::vector<Location>> Server::text_document_references(const TextDocumentIdentifier& id,
                                                                      const Position& position,
                                                                      bool include_declaration) {
    EditorView* view = view_for(id.uri);
    if (view == nullptr) {
        return std::nullopt;
    }
    std::vector<Location> locations;
    const auto add = [&locations](Location location) {
        const bool repeated = std::ranges::any_of(locations, [&](const Location& known) {
            return known.uri == location.uri && known.range.start.line == location.range.start.line &&
                   known.range.start.character == location.range.start.character;
        });
        if (!repeated) {
            locations.push_back(std::move(location));
        }
    };
    if (const std::optional<EditorView::Target> target = view->target_at(position)) {
        std::vector<std::string> uris;
        documents_.for_each([&uris](const Document& document) { uris.push_back(document.uri()); });
        for (const std::string& uri : uris) {
            EditorView* other = view_for(uri);
            if (other == nullptr) {
                continue;
            }
            for (EditorView::Mention& mention : other->mentions(*target)) {
                if (include_declaration || mention.role != clangbridge::Role::Declaration) {
                    add(std::move(mention.location));
                }
            }
        }
    }
    // What proof statements name is the compiler's to resolve, not Clang's.
    const ProofNames names(documents_);
    if (const std::optional<ProofNames::Declaration> named = names.declaration_at(*documents_.get(id.uri), position)) {
        if (include_declaration) {
            if (std::optional<Location> declared = names.locate(*named)) {
                add(std::move(*declared));
            }
        }
        for (Location& use : names.uses_of(*named)) {
            add(std::move(use));
        }
    }
    std::ranges::sort(locations, [](const Location& lhs, const Location& rhs) {
        if (lhs.uri != rhs.uri) {
            return lhs.uri < rhs.uri;
        }
        if (lhs.range.start.line != rhs.range.start.line) {
            return lhs.range.start.line < rhs.range.start.line;
        }
        return lhs.range.start.character < rhs.range.start.character;
    });
    return locations;
}

std::optional<std::vector<DocumentHighlight>> Server::text_document_document_highlight(const TextDocumentIdentifier& id,
                                                                                       const Position& position) {
    EditorView* view = view_for(id.uri);
    if (view == nullptr) {
        return std::nullopt;
    }
    std::vector<DocumentHighlight> highlights;
    const auto add = [&](const Location& location, DocumentHighlightKind kind) {
        if (location.uri != id.uri) {
            return;
        }
        const bool repeated = std::ranges::any_of(highlights, [&](const DocumentHighlight& known) {
            return known.range.start.line == location.range.start.line &&
                   known.range.start.character == location.range.start.character;
        });
        if (!repeated) {
            highlights.push_back(DocumentHighlight{location.range, kind});
        }
    };
    if (const std::optional<EditorView::Target> target = view->target_at(position)) {
        for (const EditorView::Mention& mention : view->mentions(*target)) {
            switch (mention.role) {
                case clangbridge::Role::Declaration:
                    add(mention.location, DocumentHighlightKind::Text);
                    break;
                case clangbridge::Role::Read:
                    add(mention.location, DocumentHighlightKind::Read);
                    break;
                case clangbridge::Role::Write:
                    add(mention.location, DocumentHighlightKind::Write);
                    break;
            }
        }
    }
    const ProofNames names(documents_);
    if (const std::optional<ProofNames::Declaration> named = names.declaration_at(*documents_.get(id.uri), position)) {
        if (const std::optional<Location> declared = names.locate(*named)) {
            add(*declared, DocumentHighlightKind::Text);
        }
        for (const Location& use : names.uses_of(*named)) {
            add(use, DocumentHighlightKind::Read);
        }
    }
    std::ranges::sort(highlights, [](const DocumentHighlight& lhs, const DocumentHighlight& rhs) {
        if (lhs.range.start.line != rhs.range.start.line) {
            return lhs.range.start.line < rhs.range.start.line;
        }
        return lhs.range.start.character < rhs.range.start.character;
    });
    return highlights;
}

std::optional<std::vector<Location>> Server::text_document_navigate(clangbridge::Destination destination,
                                                                    const TextDocumentIdentifier& id,
                                                                    const Position& position) {
    EditorView* view = view_for(id.uri);
    if (view == nullptr) {
        return std::nullopt;
    }
    std::vector<Location> locations = view->navigate(destination, position);
    // A name a proof statement uses is C++L, and Clang has nothing to say about
    // it; the compiler resolved it.
    if (locations.empty() &&
        (destination == clangbridge::Destination::Definition || destination == clangbridge::Destination::Declaration)) {
        const ProofNames names(documents_);
        if (const std::optional<ProofNames::Declaration> named =
                names.declaration_at(*documents_.get(id.uri), position)) {
            if (std::optional<Location> declared = names.locate(*named)) {
                locations.push_back(std::move(*declared));
            }
        }
    }
    return locations;
}

std::vector<CodeAction> Server::text_document_code_actions(const CodeActionRequest& request) {
    const Document* doc = documents_.get(request.document.uri);
    if (doc == nullptr) {
        return {};
    }
    formatter::FormatRequest format;
    format.text = doc->text();
    format.virtual_path = doc->path();

    std::vector<CodeAction> actions;
    if (asks_for(request.only, kQuickFixKind)) {
        const PositionMapper mapper(doc->text());
        const std::size_t from = mapper.position_to_byte_offset(request.range.start);
        const std::size_t to = mapper.position_to_byte_offset(request.range.end);
        for (const formatter::SyntaxFix& fix : formatter::syntax_fixes(format)) {
            const bool touches = std::ranges::any_of(fix.edits, [&](const formatter::FormatEdit& edit) {
                return edit.span.offset <= to && from <= edit.span.end();
            });
            if (touches) {
                actions.push_back({fix.title, std::string(kQuickFixKind), to_text_edits(format.text, fix.edits)});
            }
        }
    }
    const bool fix_all_asked = request.only.empty() ? !request.automatic : asks_for(request.only, kFixAllKind);
    if (fix_all_asked) {
        const formatter::FormatResult formatted = formatter::format_document(format);
        if (formatted.ok && !formatted.edits.empty()) {
            actions.push_back({"Format canonical C++L syntax", std::string(kFixAllKind),
                               to_text_edits(format.text, formatted.edits)});
        }
    }
    return actions;
}

std::optional<std::vector<TextEdit>> Server::text_document_formatting(const TextDocumentIdentifier& id) {
    const Document* doc = documents_.get(id.uri);
    if (doc == nullptr) {
        return std::nullopt;
    }

    formatter::FormatRequest request;
    request.text = doc->text();
    request.clang_format.clear(); // cppl::formatter falls back to its own configured default
    request.virtual_path = doc->path();

    const formatter::FormatResult result = formatter::format_document(request);
    if (!result.ok) {
        return std::vector<TextEdit>{}; // known document, formatting failed: no edits rather than an error response
    }
    return to_text_edits(doc->text(), result.edits);
}

std::optional<std::vector<TextEdit>> Server::text_document_range_formatting(const TextDocumentIdentifier& id,
                                                                            const Range& range) {
    const Document* doc = documents_.get(id.uri);
    if (doc == nullptr) {
        return std::nullopt;
    }

    const PositionMapper mapper(doc->text());
    const std::size_t start = mapper.position_to_byte_offset(range.start);
    const std::size_t end = mapper.position_to_byte_offset(range.end);

    formatter::FormatRequest request;
    request.text = doc->text();
    request.virtual_path = doc->path();

    const formatter::FormatResult result =
        formatter::format_ranges(request, {source::ByteSpan{start, end > start ? end - start : 0}});
    if (!result.ok) {
        return std::vector<TextEdit>{};
    }
    return to_text_edits(doc->text(), result.edits);
}

std::optional<std::vector<TextEdit>> Server::text_document_on_type_formatting(const TextDocumentIdentifier& id,
                                                                              const Position& position,
                                                                              const std::string& trigger_character) {
    const Document* doc = documents_.get(id.uri);
    if (doc == nullptr) {
        return std::nullopt;
    }

    const PositionMapper mapper(doc->text());
    const std::size_t offset = mapper.position_to_byte_offset(position);

    formatter::FormatRequest request;
    request.text = doc->text();
    request.virtual_path = doc->path();

    const formatter::FormatResult result = formatter::format_on_type(request, offset, trigger_character);
    if (!result.ok) {
        return std::vector<TextEdit>{};
    }
    return to_text_edits(doc->text(), result.edits);
}

CompletionList Server::text_document_completion(const TextDocumentIdentifier& id, const Position& position) {
    const Document* doc = documents_.get(id.uri);
    if (doc == nullptr) {
        return {};
    }
    if (doc->syntax() != nullptr) {
        const PositionMapper mapper(doc->text());
        const std::optional<CaseSite> site =
            enclosing_case_site(*doc->syntax(), doc->subject_states(), mapper.position_to_byte_offset(position));
        if (site) {
            // The arms still owed are the whole answer: the list is the
            // provider's complete set.
            return CompletionList{false, missing_case_completions(*site)};
        }
    }
    EditorView* view = view_for(id.uri);
    if (view == nullptr) {
        return {};
    }
    return view->complete(position, client_.snippets);
}

std::optional<SignatureHelp> Server::text_document_signature_help(const TextDocumentIdentifier& id,
                                                                  const Position& position) {
    EditorView* view = view_for(id.uri);
    if (view == nullptr) {
        return std::nullopt;
    }
    return view->signature_help(position);
}

std::optional<Hover> Server::text_document_hover(const TextDocumentIdentifier& id, const Position& position) {
    const Document* doc = documents_.get(id.uri);
    if (doc == nullptr) {
        return std::nullopt;
    }
    if (doc->syntax() != nullptr) {
        const PositionMapper mapper(doc->text());
        const std::optional<CaseSite> site =
            enclosing_case_site(*doc->syntax(), doc->subject_states(), mapper.position_to_byte_offset(position));
        if (site) {
            return case_site_hover(*site);
        }
    }
    EditorView* view = view_for(id.uri);
    if (view == nullptr) {
        return std::nullopt;
    }
    // A C++L declaration is shown as written, with what became of its
    // obligations in the last compile.
    const auto with_status = [doc](const CpplDeclaration& declaration, Hover hover) {
        hover.contents += verification_markdown(*doc, declaration);
        return hover;
    };
    // A name a proof statement uses, shown as the declaration the compiler
    // resolved it to.
    const ProofNames names(documents_);
    if (const std::optional<ProofNames::Declaration> named = names.declaration_at(*doc, position)) {
        if (const std::optional<Location> declared = names.locate(*named)) {
            if (std::optional<CpplDeclaration> written = view->cppl_declaration_at(*declared)) {
                Hover hover;
                hover.contents = written->markdown;
                return with_status(*written, std::move(hover));
            }
        }
    }
    std::optional<EditorView::HoverAnswer> answer = view->hover(position);
    if (!answer.has_value()) {
        return std::nullopt;
    }
    if (answer->declaration.has_value()) {
        return with_status(*answer->declaration, std::move(answer->hover));
    }
    return std::move(answer->hover);
}

std::optional<std::vector<CodeLens>> Server::text_document_code_lens(const TextDocumentIdentifier& id) const {
    const Document* doc = documents_.get(id.uri);
    if (doc == nullptr) {
        return std::nullopt;
    }
    return verification_lenses(*doc);
}

std::optional<std::vector<std::uint32_t>> Server::text_document_semantic_tokens(
    const TextDocumentIdentifier& id) const {
    const Document* doc = documents_.get(id.uri);
    if (doc == nullptr) {
        return std::nullopt;
    }
    if (doc->syntax() == nullptr) {
        return std::vector<std::uint32_t>{};
    }
    return proof_keyword_tokens(*doc->syntax(), doc->text(), doc->path_claims_recognized(),
                                doc->path_splits_recognized());
}

void Server::publish_diagnostics(const Document& doc) {
    // This runs the compile that also records the document's decomposition
    // states, which completion and hover answer from. Returning early when no
    // publisher is installed would leave those states empty and make
    // completion silently offer nothing, so the compile happens either way and
    // only the delivery at the end is conditional.
    Document* mutable_doc = documents_.get(doc.uri());

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
    driver::BufferCompileOutcome outcome = driver::compile_buffer(request, engine);
    if (mutable_doc != nullptr) {
        mutable_doc->set_subject_states(std::move(outcome.subject_states));
        mutable_doc->set_resolved_names(std::move(outcome.names));
        mutable_doc->set_verification(outcome.verified, std::move(outcome.obligations), doc.version());
        mutable_doc->set_path_claims_recognized(outcome.syntax != nullptr &&
                                                !outcome.syntax->path_contradictions.empty());
        mutable_doc->set_path_splits_recognized(outcome.syntax != nullptr && !outcome.syntax->path_splits.empty());
    }

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

        const PublishedDocument published{doc.path(), doc.uri(), outcome.tokens.get()};
        lsp_diagnostics = linter_.lint(*outcome.tokens, *outcome.syntax, syntax_diagnostics, mapper, published);
        for (const diagnostics::Diagnostic& diagnostic : other_diagnostics) {
            lsp_diagnostics.push_back(linter_.convert_diagnostic(diagnostic, mapper, published));
        }
        // The same clause-placement rule the formatter enforces, reported as
        // Style warnings; cheap (no clang-format subprocess) so it runs on
        // every publish rather than only when the user asks to format. A
        // header's layout is the header's own to report.
        for (const diagnostics::Diagnostic& diagnostic : formatter::check_style(*outcome.tokens, *outcome.syntax)) {
            if (published.holds(diagnostic.location)) {
                lsp_diagnostics.push_back(linter_.convert_diagnostic(diagnostic, mapper, published));
            }
        }
    } else {
        // The pipeline could not even preprocess the buffer (missing clang,
        // a broken #include, ...): fall back to the document's own
        // structural parse, if it has one, so the editor still sees
        // whatever C++L-aware diagnostics are available, and report every
        // engine diagnostic (preprocessing failures) directly.
        const PublishedDocument published{doc.path(), doc.uri(), doc.tokens()};
        if (doc.tokens() && doc.syntax()) {
            lsp_diagnostics = linter_.lint(*doc.tokens(), *doc.syntax(), doc.diagnostics(), mapper, published);
            for (const diagnostics::Diagnostic& diagnostic : formatter::check_style(*doc.tokens(), *doc.syntax())) {
                lsp_diagnostics.push_back(linter_.convert_diagnostic(diagnostic, mapper, published));
            }
        }
        for (const diagnostics::Diagnostic& diagnostic : engine.diagnostics()) {
            lsp_diagnostics.push_back(linter_.convert_diagnostic(diagnostic, mapper, published));
        }
    }

    if (diagnostic_publisher_) {
        diagnostic_publisher_(doc.uri(), std::move(lsp_diagnostics));
    }
}

} // namespace cppl::lsp
