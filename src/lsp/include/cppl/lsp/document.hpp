#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/protocol.hpp"

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
    void apply_change(const TextDocumentContentChangeEvent& change, std::int32_t version);

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

    // Whether the last full compile of this buffer recognized a `contradiction`
    // statement in a verified body as a claim that its path cannot occur. That
    // depends on every use of the word in the translation unit, headers
    // included, which `reparse` cannot see (semantic_tokens.hpp).
    [[nodiscard]] bool path_claims_recognized() const noexcept {
        return path_claims_recognized_;
    }
    void set_path_claims_recognized(bool recognized) noexcept {
        path_claims_recognized_ = recognized;
    }

    // The same, for a `cases` or `decompose` statement in a verified body read
    // as a case split on its path (SPEC.md CASE-017).
    [[nodiscard]] bool path_splits_recognized() const noexcept {
        return path_splits_recognized_;
    }
    void set_path_splits_recognized(bool recognized) noexcept {
        path_splits_recognized_ = recognized;
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
    bool path_claims_recognized_ = false;
    bool path_splits_recognized_ = false;
};

// Manages all open documents
class DocumentManager {
  public:
    void open(TextDocumentItem item);
    void change(const VersionedTextDocumentIdentifier& id, const std::vector<TextDocumentContentChangeEvent>& changes);
    void close(const TextDocumentIdentifier& id);

    [[nodiscard]] Document* get(const std::string& uri);
    [[nodiscard]] const Document* get(const std::string& uri) const;

    template <typename Visit> void for_each(Visit&& visit) const {
        for (const auto& [uri, document] : documents_) {
            visit(static_cast<const Document&>(*document));
        }
    }

  private:
    std::unordered_map<std::string, std::unique_ptr<Document>> documents_;
};

} // namespace cppl::lsp
