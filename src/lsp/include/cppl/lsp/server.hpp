#pragma once

#include "cppl/clang/editor.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/buffer_compile.hpp"
#include "cppl/lsp/compile_commands.hpp"
#include "cppl/lsp/document.hpp"
#include "cppl/lsp/editor_view.hpp"
#include "cppl/lsp/linter.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/workspace_index.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppl::lsp {

// One compile of one version of a document: what it reads, taken where the
// server runs, and what it produced, applied there.
struct CompileJob {
    std::string uri;
    driver::BufferCompileRequest request;
};

struct CompileResult {
    std::string uri;
    // The text compiled, which the result describes.
    std::string text;
    driver::BufferCompileOutcome outcome;
    std::vector<diagnostics::Diagnostic> diagnostics;
};

// Runs `job`'s compile. It reads no server state, so it may run on any thread.
[[nodiscard]] CompileResult compile(const CompileJob& job);

// LSP server state and request handlers
class Server {
  public:
    // `clang` is the Clang driver executable used to preprocess and
    // semantically check buffers (mirrors `cppl::driver::Options::clang`).
    // Each document is read with the flags its build compiles it with, from
    // the nearest `compile_commands.json` (compile_commands.hpp), followed by
    // `clang_arguments`, extra flags for every document; it may be empty.
    explicit Server(std::string clang = {}, std::vector<std::string> clang_arguments = {});

    // Lifecycle. With workspace `roots`, every file under them is indexed in
    // the background (workspace_index.hpp), reporting how far it has come to
    // the progress set beforehand.
    void initialize(ClientCapabilities capabilities = {}, std::vector<std::string> roots = {});
    void set_index_progress(WorkspaceIndex::Progress progress) {
        index_progress_ = std::move(progress);
    }
    void initialized();
    void shutdown();
    void exit();

    // Ends indexing: after this the index reports nothing more.
    void stop_index() {
        index_.reset();
    }
    [[nodiscard]] bool indexing() const noexcept {
        return index_ != nullptr;
    }

    // Returns once the workspace's files have all been indexed as they are on
    // disk now, true, or `timeout` has passed, false. True with no workspace.
    [[nodiscard]] bool wait_for_index(std::chrono::milliseconds timeout) {
        return index_ == nullptr || index_->wait_until_current(timeout);
    }

    // The declarations in the workspace whose name matches `query` -- its
    // characters in order, ignoring case -- best first: the index's, with an
    // open document's taken from the document as the editor holds it (LSP
    // `workspace/symbol`).
    [[nodiscard]] std::vector<WorkspaceIndex::Symbol> workspace_symbols(const std::string& query);

    [[nodiscard]] bool is_shutting_down() const noexcept {
        return shutting_down_;
    }
    [[nodiscard]] bool should_exit() const noexcept {
        return should_exit_;
    }

    // Where compiles run. With none set, a document is compiled as it opens
    // or changes, before the notification returns. With one set, each compile
    // is handed to it, and `apply_compile` takes what it produced; `opened`
    // says the document has just opened, so its compile should not wait for
    // typing to pause.
    using CompileScheduler = std::function<void(CompileJob job, bool opened)>;
    void set_compile_scheduler(CompileScheduler scheduler) {
        compile_scheduler_ = std::move(scheduler);
    }

    // Records what a compile found and publishes its diagnostics, when the
    // document still holds the text it compiled. A compile of text since
    // edited is dropped: a compile of the edit is on its way.
    void apply_compile(CompileResult result);

    // Document synchronization
    void text_document_did_open(const TextDocumentItem& item);
    void text_document_did_change(const VersionedTextDocumentIdentifier& id,
                                  const std::vector<TextDocumentContentChangeEvent>& changes);
    void text_document_did_close(const TextDocumentIdentifier& id);

    // Formatting. All three share cppl::formatter's format_document/
    // format_ranges/format_on_type with the CLI (tools/cppl-format), so
    // canonical output is identical on both surfaces
    // (tools/cppl-lsp/README.md). `std::nullopt` means the document is
    // unknown; an empty (non-null) vector means it is known but already
    // canonical, matching how LSP distinguishes "no edits" from "no such
    // document" through a null vs. empty result.
    [[nodiscard]] std::optional<std::vector<TextEdit>> text_document_formatting(const TextDocumentIdentifier& id);
    [[nodiscard]] std::optional<std::vector<TextEdit>> text_document_range_formatting(const TextDocumentIdentifier& id,
                                                                                      const Range& range);
    [[nodiscard]] std::optional<std::vector<TextEdit>> text_document_on_type_formatting(
        const TextDocumentIdentifier& id, const Position& position, const std::string& trigger_character);

    // Code actions over the same formatter engine: each syntax migration whose
    // edits touch `request.range`, as a quick fix, and canonical formatting of
    // the whole document as a fix-all. Formatting runs clang-format, so it is
    // computed only when asked for by kind or by the user, never as the cursor
    // moves.
    [[nodiscard]] std::vector<CodeAction> text_document_code_actions(const CodeActionRequest& request);

    // Proof-decomposition assistance.
    //
    // Both answer from the states the compiler's own case engine recorded for
    // this buffer (`Document::subject_states`), never from a decomposition
    // this server performed: there is one case engine and it is the
    // compiler's (`AGENTS.md` 39). Where the compiler has not confirmed a
    // subject's states -- it could not reach elaboration, or no provider
    // models the type -- these return nothing rather than guess.
    //
    //
    // Everywhere else, completion is Clang's names and keywords with C++L's
    // own words where the grammar admits them (completion.hpp).
    [[nodiscard]] CompletionList text_document_completion(const TextDocumentIdentifier& id, const Position& position);

    // The declarations the call being written at `position` could resolve to,
    // from Clang (LSP `textDocument/signatureHelp`). Nothing outside a call, or
    // for an unknown document.
    [[nodiscard]] std::optional<SignatureHelp> text_document_signature_help(const TextDocumentIdentifier& id,
                                                                            const Position& position);

    // Hover: inside a `cases`/`decompose` block, the subject's states, as
    // above; on a name a proof statement uses, the declaration the compiler
    // resolved it to; on any other name, Clang's description of it, or the
    // C++L declaration it stands for (tools/cppl-lsp/README.md, "Hover").
    [[nodiscard]] std::optional<Hover> text_document_hover(const TextDocumentIdentifier& id, const Position& position);

    // A lens over every Law, proof and verified function the document writes,
    // stating what became of its obligations in the last compile (LSP
    // `textDocument/codeLens`). `std::nullopt` means the document is unknown.
    [[nodiscard]] std::optional<std::vector<CodeLens>> text_document_code_lens(const TextDocumentIdentifier& id) const;

    // Every name the document writes, as the kind of thing it names -- C++
    // from Clang, C++L's words and names from the recognizer and the compile
    // -- as encoded semantic tokens (semantic_tokens.hpp). `std::nullopt` means
    // the document is unknown.
    [[nodiscard]] std::optional<std::vector<std::uint32_t>> text_document_semantic_tokens(
        const TextDocumentIdentifier& id);

    // Where the name at `position` is defined, declared, typed or overridden,
    // as `destination` asks (LSP `textDocument/definition`, `declaration`,
    // `typeDefinition`, `implementation`). Clang answers, over the document's
    // projection, and only positions traced back to text an author wrote are
    // returned (tools/cppl-lsp/README.md, "Navigation"). `std::nullopt` means
    // the document is unknown.
    [[nodiscard]] std::optional<std::vector<Location>> text_document_navigate(clangbridge::Destination destination,
                                                                              const TextDocumentIdentifier& id,
                                                                              const Position& position);

    // Every place the name at `position` is written, in every open document's
    // unit and the headers each includes, ordered by document and position
    // (LSP `textDocument/references`). Declarations are left out unless
    // `include_declaration`. `std::nullopt` means the document is unknown.
    [[nodiscard]] std::optional<std::vector<Location>> text_document_references(const TextDocumentIdentifier& id,
                                                                                const Position& position,
                                                                                bool include_declaration);

    // Rename (LSP `textDocument/prepareRename` and `textDocument/rename`;
    // tools/cppl-lsp/README.md, "Rename"). A rename rewrites every place
    // references finds the name written, declarations included, or none of
    // them. It is refused, saying why, unless every place still spells the
    // name and lies in a file the editor holds open or the workspace index
    // reads, the new name is a C++ identifier and no keyword, and each file's
    // C++L reads the same afterwards (rename.hpp). Preparing names the place
    // under the cursor a rename would rewrite.
    [[nodiscard]] std::expected<PrepareRename, std::string> text_document_prepare_rename(
        const TextDocumentIdentifier& id, const Position& position);
    [[nodiscard]] std::expected<WorkspaceEdit, std::string> text_document_rename(const TextDocumentIdentifier& id,
                                                                                 const Position& position,
                                                                                 const std::string& new_name);

    // The same, in this document only, each marked as a declaration (text), a
    // read or a write (LSP `textDocument/documentHighlight`).
    [[nodiscard]] std::optional<std::vector<DocumentHighlight>> text_document_document_highlight(
        const TextDocumentIdentifier& id, const Position& position);

    // The document's outline: every C++ declaration Clang finds whose name
    // the document writes, outside function bodies, nested as declared, and
    // every Law, proof and refinement type among them (LSP
    // `textDocument/documentSymbol`). `std::nullopt` means the document is
    // unknown.
    [[nodiscard]] std::optional<std::vector<DocumentSymbol>> text_document_document_symbol(
        const TextDocumentIdentifier& id);

    // Where the document may fold: each body, `#include` run and conditional
    // branch Clang parsed, each C++L body and declaration the recognizer found,
    // and each comment block (LSP `textDocument/foldingRange`). `std::nullopt`
    // means the document is unknown.
    [[nodiscard]] std::optional<std::vector<FoldingRange>> text_document_folding_range(
        const TextDocumentIdentifier& id);

    // For each of `positions`, what selecting outward from it selects,
    // innermost first (LSP `textDocument/selectionRange`). `std::nullopt` means
    // the document is unknown.
    [[nodiscard]] std::optional<std::vector<std::vector<Range>>> text_document_selection_range(
        const TextDocumentIdentifier& id, const std::vector<Position>& positions);

    // The parameter names and deduced types Clang gives for `range` of the
    // document (LSP `textDocument/inlayHint`). `std::nullopt` means the
    // document is unknown.
    [[nodiscard]] std::optional<std::vector<InlayHint>> text_document_inlay_hint(const TextDocumentIdentifier& id,
                                                                                 const Range& range);

    [[nodiscard]] const ClientCapabilities& client_capabilities() const noexcept {
        return client_;
    }

    // Diagnostics
    using DiagnosticPublisher = std::function<void(const std::string& uri, std::vector<Diagnostic>)>;
    void set_diagnostic_publisher(DiagnosticPublisher publisher) {
        diagnostic_publisher_ = std::move(publisher);
    }

    [[nodiscard]] TextDocumentSyncKind sync_kind() const noexcept {
        return sync_kind_;
    }

  private:
    // Compiles the document, where the scheduler says or at once, and
    // publishes what the compile found.
    void publish_diagnostics(const Document& doc, bool opened);

    // The document's view, brought up to date with every open buffer first;
    // null when the document is unknown.
    EditorView* view_for(const std::string& uri);

    // The flags the document at `path` is read with, by its editor unit and by
    // its compile alike (ARCHITECTURE.md ARCH-LSP-008).
    [[nodiscard]] std::vector<std::string> arguments_for(const std::string& path);

    // A place a name is written, and whether it is declared there; or, when
    // `unwritten`, a place it is used through a macro whose body spells it.
    struct Occurrence {
        Location location;
        bool declaration = false;
        bool unwritten = false;
    };

    // Every place the name at `position` of the open document `uri` is
    // written: in every open document's unit and the headers it includes, in
    // the files the workspace index reads, and in each proof statement the
    // compile resolved to it. Empty where no name is. `renaming` adds what a
    // rename must account for too: a class's constructors and destructor,
    // where the name is declared wherever that is, and each use a macro's
    // body spells.
    [[nodiscard]] std::vector<Occurrence> occurrences(const std::string& uri, const Position& position,
                                                      bool renaming = false);

    // What renaming the name at `position` rewrites, file by file, once every
    // place has been found to be one a rename may rewrite.
    struct RenamePlan {
        std::string name;
        Range here;
        struct File {
            // The URI an edit names: the client's own, for an open document.
            std::string uri;
            std::string text;
            std::vector<Range> ranges;
        };
        std::map<std::string, File> files; // by normal path
    };
    [[nodiscard]] std::expected<RenamePlan, std::string> plan_rename(const TextDocumentIdentifier& id,
                                                                     const Position& position);

    DocumentManager documents_;
    ClientCapabilities client_;
    CompileCommands compile_commands_;
    // One view per open document, each with the buffer generation and the
    // flags it was last refreshed with. Any edit to any open buffer can change
    // what another document's unit reads, so every edit starts a new
    // generation; an edited compilation database can change the flags.
    struct View {
        std::unique_ptr<EditorView> view;
        std::uint64_t generation = 0;
        std::vector<std::string> arguments;
    };
    std::unordered_map<std::string, View> views_;
    std::uint64_t generation_ = 1;
    Linter linter_;
    DiagnosticPublisher diagnostic_publisher_;
    CompileScheduler compile_scheduler_;
    WorkspaceIndex::Progress index_progress_;
    std::unique_ptr<WorkspaceIndex> index_;
    TextDocumentSyncKind sync_kind_ = TextDocumentSyncKind::Incremental;
    bool shutting_down_ = false;
    bool should_exit_ = false;
    std::string clang_;
    std::vector<std::string> clang_arguments_;
    // The Clang driver by path, for the editor units: Clang finds the standard
    // library and system headers relative to it.
    std::string driver_;
};

} // namespace cppl::lsp
