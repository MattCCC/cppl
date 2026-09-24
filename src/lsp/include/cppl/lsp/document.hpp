#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/buffer_compile.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/semantic_tokens.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppl::lsp {

// Tracks a single open document with its text, version, and parsed state
class Document {
  public:
    Document(std::string uri, std::string text, std::int32_t version);

    void update(std::string text, std::int32_t version);
    // A change with a range replaces what it covers, counted in UTF-16 units
    // as LSP counts them; one without replaces the whole text. Several apply
    // in order, each to what the last left, and are recognized once.
    void apply_change(const TextDocumentContentChangeEvent& change, std::int32_t version);
    void apply_changes(const std::vector<TextDocumentContentChangeEvent>& changes, std::int32_t version);

    [[nodiscard]] const std::string& uri() const noexcept {
        return uri_;
    }
    [[nodiscard]] const std::string& text() const noexcept {
        return text_;
    }
    [[nodiscard]] std::int32_t version() const noexcept {
        return version_;
    }

    // The native filesystem path the URI resolved to, used as the virtual
    // path handed to the buffer-compile pipeline (for diagnostics and
    // #include resolution). Falls back to the raw URI when it could not be
    // resolved to a file:// path.
    [[nodiscard]] const std::string& path() const noexcept {
        return path_;
    }

    // Reparse the document and update cached structures
    void reparse();

    [[nodiscard]] const frontend::TokenStream* tokens() const noexcept {
        return tokens_.get();
    }
    [[nodiscard]] const frontend::Syntax* syntax() const noexcept {
        return syntax_.get();
    }
    [[nodiscard]] const std::vector<diagnostics::Diagnostic>& diagnostics() const noexcept {
        return diagnostics_;
    }

    // What the generic case engine decided each `cases`/`decompose` subject's
    // states were, as of the last full compile of this buffer.
    //
    // Recognition (`reparse`) is cheap and runs on every edit; elaboration is
    // not, and runs only where the server already pays for it, on publish. So
    // these states can lag the text by one edit: a label offered here was
    // confirmed by the compiler for a slightly older version of the buffer.
    // That is the right trade for completion -- the alternative is either a
    // Clang round trip per keystroke or a second decomposition engine, and
    // `AGENTS.md` 39 forbids the second.
    [[nodiscard]] const std::vector<elaboration::SubjectStates>& subject_states() const noexcept {
        return subject_states_;
    }
    void set_subject_states(std::vector<elaboration::SubjectStates> states) {
        subject_states_ = std::move(states);
    }

    // What each name a proof statement uses resolved to, as of the last full
    // compile. Like `subject_states`, these can lag the text by an edit, so a
    // reader checks the buffer still spells a name where one is recorded.
    [[nodiscard]] const std::vector<elaboration::ResolvedName>& resolved_names() const noexcept {
        return resolved_names_;
    }
    void set_resolved_names(std::vector<elaboration::ResolvedName> names) {
        resolved_names_ = std::move(names);
    }

    // What became of each obligation the last full compile verified, whether
    // that compile reached verification at all, and the buffer version it
    // compiled. A reader shows these only for the version they describe.
    [[nodiscard]] const std::vector<driver::ObligationRecord>& obligations() const noexcept {
        return obligations_;
    }
    [[nodiscard]] bool verified() const noexcept {
        return verified_;
    }
    [[nodiscard]] std::int32_t verified_version() const noexcept {
        return verified_version_;
    }
    void set_verification(bool verified, std::vector<driver::ObligationRecord> obligations, std::int32_t version) {
        verified_ = verified;
        obligations_ = std::move(obligations);
        verified_version_ = version;
    }

    // Which words the last full compile of this buffer read as C++L where that
    // depends on every use of the word in the translation unit, headers
    // included, which `reparse` cannot see (semantic_tokens.hpp).
    [[nodiscard]] UnitRecognition recognized() const noexcept {
        return recognized_;
    }
    void set_recognized(UnitRecognition recognized) noexcept {
        recognized_ = recognized;
    }

  private:
    std::string uri_;
    std::string path_;
    std::string text_;
    std::int32_t version_ = 0;

    // Cached parse results
    std::unique_ptr<frontend::TokenStream> tokens_;
    std::unique_ptr<frontend::Syntax> syntax_;
    std::vector<diagnostics::Diagnostic> diagnostics_;
    std::vector<elaboration::SubjectStates> subject_states_;
    std::vector<elaboration::ResolvedName> resolved_names_;
    std::vector<driver::ObligationRecord> obligations_;
    bool verified_ = false;
    std::int32_t verified_version_ = -1;
    UnitRecognition recognized_;
};

// Manages all open documents
class DocumentManager {
  public:
    void open(TextDocumentItem item);
    void change(const VersionedTextDocumentIdentifier& id, const std::vector<TextDocumentContentChangeEvent>& changes);
    void close(const TextDocumentIdentifier& id);

    [[nodiscard]] Document* get(const std::string& uri);
    [[nodiscard]] const Document* get(const std::string& uri) const;

    template <typename Visit> void for_each(const Visit& visit) const {
        for (const auto& [uri, document] : documents_) {
            visit(static_cast<const Document&>(*document));
        }
    }

  private:
    std::unordered_map<std::string, std::unique_ptr<Document>> documents_;
};

} // namespace cppl::lsp
