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
#include "cppl/lsp/rename.hpp"
#include "cppl/lsp/semantic_tokens.hpp"
#include "cppl/lsp/uri.hpp"
#include "cppl/lsp/verification.hpp"
#include "cppl/lsp/workspace_index.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
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

// The file a URI names, in the form two spellings of it share.
std::string file_of(const std::string& uri) {
    return normal_path(uri_to_path(uri).value_or(uri));
}

// The documents open now, known by file rather than by how a URI spells it:
// a client and path_to_uri may escape the same path differently.
class OpenFiles {
  public:
    explicit OpenFiles(const DocumentManager& documents) {
        documents.for_each([this](const Document& document) {
            uris_.push_back(document.uri());
            documents_.emplace(file_of(document.uri()), &document);
        });
    }

    [[nodiscard]] const std::vector<std::string>& uris() const noexcept {
        return uris_;
    }

    [[nodiscard]] bool holds(const std::string& uri) const {
        return documents_.contains(file_of(uri));
    }

    // The open document of the file `uri` names, however the URI spells it.
    [[nodiscard]] const Document* document(const std::string& uri) const {
        const auto found = documents_.find(file_of(uri));
        return found == documents_.end() ? nullptr : found->second;
    }

  private:
    std::vector<std::string> uris_;
    std::map<std::string, const Document*, std::less<>> documents_;
};

bool before(const Position& lhs, const Position& rhs) {
    return lhs.line < rhs.line || (lhs.line == rhs.line && lhs.character <= rhs.character);
}

// Whether `range` holds `position`, its end included: a cursor just past a
// name is on it.
bool holds(const Range& range, const Position& position) {
    return before(range.start, position) && before(position, range.end);
}

// The text `range` covers.
std::string spelled(const PositionMapper& mapper, const std::string& text, const Range& range) {
    const std::size_t start = mapper.position_to_byte_offset(range.start);
    const std::size_t end = mapper.position_to_byte_offset(range.end);
    return start <= end && end <= text.size() ? text.substr(start, end - start) : std::string();
}

} // namespace

Server::Server(std::string clang, std::vector<std::string> clang_arguments)
    : clang_(std::move(clang)),
      clang_arguments_(std::move(clang_arguments)),
      driver_(resolve_driver(clang_.empty() ? std::string{CPPL_DEFAULT_CLANG} : clang_)) {}

void Server::initialize(ClientCapabilities capabilities, std::vector<std::string> roots) {
    client_ = capabilities;
    if (!roots.empty()) {
        WorkspaceIndex::Options options;
        options.driver = driver_;
        options.clang = clang_;
        options.clang_arguments = clang_arguments_;
        std::vector<std::filesystem::path> paths(roots.begin(), roots.end());
        index_ = std::make_unique<WorkspaceIndex>(std::move(paths), std::move(options), index_progress_);
    }
}

std::vector<WorkspaceIndex::Symbol> Server::workspace_symbols(const std::string& query) {
    constexpr std::size_t kMostSymbols = 256;
    const OpenFiles open(documents_);

    // An open document's declarations are the ones it holds now.
    std::vector<WorkspaceIndex::Symbol> found;
    for (const std::string& uri : open.uris()) {
        if (EditorView* view = view_for(uri)) {
            for (WorkspaceIndex::Symbol& symbol : WorkspaceIndex::declared(view->outline(), uri)) {
                if (WorkspaceIndex::match(symbol.name, query) > 0) {
                    found.push_back(std::move(symbol));
                }
            }
        }
    }
    if (index_ != nullptr) {
        for (WorkspaceIndex::Symbol& symbol : index_->symbols(query, kMostSymbols)) {
            if (!open.holds(symbol.location.uri)) {
                found.push_back(std::move(symbol));
            }
        }
    }
    WorkspaceIndex::rank(found, query);
    if (found.size() > kMostSymbols) {
        found.resize(kMostSymbols);
    }
    return found;
}

void Server::initialized() {
    // Client has confirmed initialization
}

void Server::shutdown() {
    shutting_down_ = true;
    stop_index();
}

void Server::exit() {
    should_exit_ = true;
}

void Server::text_document_did_open(const TextDocumentItem& item) {
    documents_.open(item);
    ++generation_;

    // Publish diagnostics for the newly opened document
    if (const auto* doc = documents_.get(item.uri)) {
        publish_diagnostics(*doc, true);
    }
}

void Server::text_document_did_change(const VersionedTextDocumentIdentifier& id,
                                      const std::vector<TextDocumentContentChangeEvent>& changes) {
    documents_.change(id, changes);
    ++generation_;

    // Publish updated diagnostics
    if (const auto* doc = documents_.get(id.uri)) {
        publish_diagnostics(*doc, false);
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
    std::vector<std::string> arguments = arguments_for(document->path());
    if (entry.generation != generation_ || entry.arguments != arguments) {
        std::vector<OpenBuffer> open;
        documents_.for_each([&open](const Document& buffer) {
            open.push_back(OpenBuffer{buffer.uri(), buffer.path(), &buffer.text()});
        });
        EditorView::Options options;
        options.driver = driver_;
        options.arguments = arguments;
        entry.view->refresh(OpenBuffer{document->uri(), document->path(), &document->text()}, open, options);
        entry.generation = generation_;
        entry.arguments = std::move(arguments);
    }
    return entry.view.get();
}

std::vector<std::string> Server::arguments_for(const std::string& path) {
    std::vector<std::string> arguments = compile_commands_.flags_for(path);
    arguments.insert(arguments.end(), clang_arguments_.begin(), clang_arguments_.end());
    return arguments;
}

std::vector<Server::Occurrence> Server::occurrences(const std::string& uri, const Position& position, bool renaming) {
    std::vector<Occurrence> found;
    EditorView* view = view_for(uri);
    const Document* document = documents_.get(uri);
    if (view == nullptr || document == nullptr) {
        return found;
    }
    std::map<std::tuple<std::string, std::uint32_t, std::uint32_t>, std::size_t> at;
    const auto add = [&found, &at](Location location, bool declaration) {
        const auto [known, added] =
            at.try_emplace({location.uri, location.range.start.line, location.range.start.character}, found.size());
        if (added) {
            found.push_back(Occurrence{std::move(location), declaration});
        } else if (declaration) {
            found[known->second].declaration = true;
        }
    };
    // An open document answers as the editor holds it; the index answers for
    // every other file of the workspace, as it is on disk.
    const OpenFiles open(documents_);
    if (std::optional<EditorView::Target> target = view->target_at(position)) {
        if (renaming) {
            // A class's constructors are spelled with its name, and where the
            // name is declared is rewritten too, even in a header no walk
            // reaches, so that a rename that cannot reach it is refused.
            target = view->renamed_together(std::move(*target));
            if (target->declaration.has_value()) {
                add(*target->declaration, true);
            }
        }
        for (const std::string& other_uri : open.uris()) {
            EditorView* other = view_for(other_uri);
            if (other == nullptr) {
                continue;
            }
            for (EditorView::Mention& mention : other->mentions(*target)) {
                add(std::move(mention.location), mention.role == clangbridge::Role::Declaration);
            }
        }
        if (index_ != nullptr) {
            for (WorkspaceIndex::Mention& mention : index_->mentions(target->usrs)) {
                if (!open.holds(mention.location.uri)) {
                    add(std::move(mention.location), mention.role == clangbridge::Role::Declaration);
                }
            }
        }
        if (renaming) {
            for (const std::string& other_uri : open.uris()) {
                if (EditorView* other = view_for(other_uri)) {
                    for (EditorView::Named& use : other->unwritten(&target->usrs)) {
                        found.push_back(Occurrence{std::move(use.mention.location), false, true});
                    }
                }
            }
            if (index_ != nullptr) {
                for (Location& use : index_->unwritten(target->usrs)) {
                    if (!open.holds(use.uri)) {
                        found.push_back(Occurrence{std::move(use), false, true});
                    }
                }
            }
        }
    }
    // What proof statements name is the compiler's to resolve, not Clang's.
    const ProofNames names(documents_);
    if (const std::optional<ProofNames::Declaration> named = names.declaration_at(*document, position)) {
        if (std::optional<Location> declared = names.locate(*named)) {
            add(std::move(*declared), true);
        }
        for (Location& use : names.uses_of(*named)) {
            add(std::move(use), false);
        }
        if (index_ != nullptr) {
            for (Location& use : index_->proof_name_uses(named->name, named->at)) {
                if (!open.holds(use.uri)) {
                    add(std::move(use), false);
                }
            }
        }
    }
    return found;
}

std::optional<std::vector<Location>> Server::text_document_references(const TextDocumentIdentifier& id,
                                                                      const Position& position,
                                                                      bool include_declaration) {
    if (view_for(id.uri) == nullptr) {
        return std::nullopt;
    }
    std::vector<Location> locations;
    for (Occurrence& occurrence : occurrences(id.uri, position)) {
        if (include_declaration || !occurrence.declaration) {
            locations.push_back(std::move(occurrence.location));
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

std::expected<Server::RenamePlan, std::string> Server::plan_rename(const TextDocumentIdentifier& id,
                                                                   const Position& position) {
    const Document* document = documents_.get(id.uri);
    if (document == nullptr) {
        return std::unexpected("the document is not open");
    }
    const std::vector<Occurrence> found = occurrences(id.uri, position, true);
    const std::string here_file = normal_path(document->path());
    const auto here = std::ranges::find_if(found, [&](const Occurrence& occurrence) {
        return !occurrence.unwritten && file_of(occurrence.location.uri) == here_file &&
               holds(occurrence.location.range, position);
    });
    if (here == found.end()) {
        return std::unexpected("there is no name here to rename");
    }
    RenamePlan plan;
    plan.here = here->location.range;
    plan.name = spelled(PositionMapper(document->text()), document->text(), plan.here);
    const std::string quoted = "'" + plan.name + "'";
    if (std::ranges::none_of(found, &Occurrence::declaration)) {
        return std::unexpected(quoted + " is declared only in text C++L generated");
    }
    // A use a macro's body spells would keep the old name, and rewriting the
    // body would rename whatever else the macro names.
    if (const auto hidden = std::ranges::find_if(found, &Occurrence::unwritten); hidden != found.end()) {
        std::string why = quoted;
        why += " is used through a macro at ";
        why += file_of(hidden->location.uri);
        why += ":" + std::to_string(hidden->location.range.start.line + 1);
        why += ":" + std::to_string(hidden->location.range.start.character + 1);
        why += ", which spells it in its body";
        return std::unexpected(std::move(why));
    }

    // Each file is rewritten as the editor holds it when it is open, and as
    // the index read it otherwise; any other file is not the workspace's.
    const OpenFiles open(documents_);
    for (const Occurrence& occurrence : found) {
        const std::string path = file_of(occurrence.location.uri);
        const auto [entry, added] = plan.files.try_emplace(path);
        RenamePlan::File& file = entry->second;
        if (added) {
            if (const Document* holder = open.document(occurrence.location.uri)) {
                file.uri = holder->uri();
                file.text = holder->text();
            } else if (index_ != nullptr && index_->holds(path)) {
                std::optional<std::string> text = read_file(path);
                if (!text.has_value()) {
                    return std::unexpected("could not read " + path);
                }
                file.uri = occurrence.location.uri;
                file.text = std::move(*text);
            } else {
                std::string why = quoted;
                why += " is also written in ";
                why += path;
                why += ", which is neither open nor in the workspace";
                return std::unexpected(std::move(why));
            }
        }
        file.ranges.push_back(occurrence.location.range);
    }
    // A place that no longer spells the name -- a file edited since it was
    // read, a name a macro spells -- is not rewritten, and so neither is any.
    // A destructor is named by `~` and its class's name, and only the name is
    // rewritten.
    for (auto& [path, file] : plan.files) {
        const PositionMapper mapper(file.text);
        for (Range& range : file.ranges) {
            if (const std::string written = spelled(mapper, file.text, range); written == "~" + plan.name) {
                ++range.start.character;
            }
            if (spelled(mapper, file.text, range) != plan.name) {
                std::string why = quoted;
                why += " is not written as such at ";
                why += path;
                why += ":" + std::to_string(range.start.line + 1);
                why += ":" + std::to_string(range.start.character + 1);
                return std::unexpected(std::move(why));
            }
        }
    }
    return plan;
}

std::expected<PrepareRename, std::string> Server::text_document_prepare_rename(const TextDocumentIdentifier& id,
                                                                               const Position& position) {
    std::expected<RenamePlan, std::string> plan = plan_rename(id, position);
    if (!plan.has_value()) {
        return std::unexpected(std::move(plan.error()));
    }
    return PrepareRename{plan->here, std::move(plan->name)};
}

std::expected<WorkspaceEdit, std::string> Server::text_document_rename(const TextDocumentIdentifier& id,
                                                                       const Position& position,
                                                                       const std::string& new_name) {
    if (std::optional<std::string> why = refuse_identifier(new_name)) {
        return std::unexpected(std::move(*why));
    }
    // A rename rewrites every place a name is written or none, so it waits
    // for the index to have read every file as it is now.
    constexpr auto kIndexWait = std::chrono::seconds(30);
    if (index_ != nullptr && !index_->wait_until_current(kIndexWait)) {
        return std::unexpected("the workspace is still being indexed; rename once indexing ends");
    }
    std::expected<RenamePlan, std::string> plan = plan_rename(id, position);
    if (!plan.has_value()) {
        return std::unexpected(std::move(plan.error()));
    }
    WorkspaceEdit edit;
    if (new_name == plan->name) {
        return edit;
    }
    for (auto& [path, file] : plan->files) {
        std::vector<TextEdit> edits;
        edits.reserve(file.ranges.size());
        for (const Range& range : file.ranges) {
            edits.push_back(TextEdit{range, new_name});
        }
        if (std::optional<std::string> why = changes_cppl(path, file.text, edits, new_name)) {
            return std::unexpected(std::move(*why));
        }
        edit.changes.insert_or_assign(file.uri, std::move(edits));
    }
    return edit;
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

std::optional<std::vector<DocumentSymbol>> Server::text_document_document_symbol(const TextDocumentIdentifier& id) {
    EditorView* view = view_for(id.uri);
    if (view == nullptr) {
        return std::nullopt;
    }
    return view->outline();
}

std::optional<std::vector<FoldingRange>> Server::text_document_folding_range(const TextDocumentIdentifier& id) {
    EditorView* view = view_for(id.uri);
    if (view == nullptr) {
        return std::nullopt;
    }
    return view->folding_ranges(client_.line_folding_only);
}

std::optional<std::vector<InlayHint>> Server::text_document_inlay_hint(const TextDocumentIdentifier& id,
                                                                       const Range& range) {
    EditorView* view = view_for(id.uri);
    if (view == nullptr) {
        return std::nullopt;
    }
    return view->inlay_hints(range);
}

std::optional<std::vector<std::vector<Range>>> Server::text_document_selection_range(
    const TextDocumentIdentifier& id, const std::vector<Position>& positions) {
    EditorView* view = view_for(id.uri);
    if (view == nullptr) {
        return std::nullopt;
    }
    std::vector<std::vector<Range>> chains;
    chains.reserve(positions.size());
    for (const Position& position : positions) {
        chains.push_back(view->selection(position));
    }
    return chains;
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

std::optional<std::vector<std::uint32_t>> Server::text_document_semantic_tokens(const TextDocumentIdentifier& id) {
    const Document* doc = documents_.get(id.uri);
    if (doc == nullptr) {
        return std::nullopt;
    }
    // C++L's words first, so that where Clang also names one -- a Law's
    // generated declaration stands for the Law's name -- the recognizer's
    // token is the one kept.
    std::vector<SemanticToken> tokens;
    if (doc->syntax() != nullptr) {
        tokens = cppl_tokens(*doc->syntax(), doc->path_claims_recognized(), doc->path_splits_recognized(),
                             doc->resolved_names());
    }
    if (const EditorView* view = view_for(id.uri)) {
        std::ranges::copy(view->semantic_tokens(), std::back_inserter(tokens));
    }
    return encode(std::move(tokens), doc->text());
}

CompileResult compile(const CompileJob& job) {
    // The real compile pipeline (preprocess -> recognize -> project -> Clang
    // parse -> elaborate -> obligations -> verify -> erase) over the live
    // buffer, exactly as the CLI compiles a file (cppl::driver::compile_buffer,
    // tools/cppl-lsp/README.md). This is the one and only place C++L
    // recognition happens for diagnostics: its tokens and syntax feed the
    // structural Linter, so the same recognition is never redone (and never
    // double-reported) by this server.
    CompileResult result;
    result.uri = job.uri;
    result.text = job.request.text;
    diagnostics::Engine engine;
    result.outcome = driver::compile_buffer(job.request, engine);
    result.diagnostics = engine.diagnostics();
    return result;
}

void Server::publish_diagnostics(const Document& doc, bool opened) {
    // This runs the compile that also records the document's decomposition
    // states, which completion and hover answer from. Returning early when no
    // publisher is installed would leave those states empty and make
    // completion silently offer nothing, so the compile happens either way and
    // only the delivery at the end is conditional.
    CompileJob job;
    job.uri = doc.uri();
    job.request.virtual_path = doc.path();
    job.request.text = doc.text();
    job.request.clang = clang_;
    job.request.clang_arguments = arguments_for(doc.path());
    if (compile_scheduler_) {
        compile_scheduler_(std::move(job), opened);
        return;
    }
    apply_compile(compile(job));
}

void Server::apply_compile(CompileResult result) {
    Document* found = documents_.get(result.uri);
    if (found == nullptr || found->text() != result.text) {
        return;
    }
    Document& doc = *found;
    driver::BufferCompileOutcome& outcome = result.outcome;
    doc.set_subject_states(std::move(outcome.subject_states));
    doc.set_resolved_names(std::move(outcome.names));
    doc.set_verification(outcome.verified, std::move(outcome.obligations), doc.version());
    doc.set_path_claims_recognized(outcome.syntax != nullptr && !outcome.syntax->path_contradictions.empty());
    doc.set_path_splits_recognized(outcome.syntax != nullptr && !outcome.syntax->path_splits.empty());

    const PositionMapper mapper(doc.text());
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
        for (const diagnostics::Diagnostic& diagnostic : result.diagnostics) {
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
        for (const diagnostics::Diagnostic& diagnostic : result.diagnostics) {
            lsp_diagnostics.push_back(linter_.convert_diagnostic(diagnostic, mapper, published));
        }
    }

    if (diagnostic_publisher_) {
        diagnostic_publisher_(doc.uri(), std::move(lsp_diagnostics));
    }
}

} // namespace cppl::lsp
