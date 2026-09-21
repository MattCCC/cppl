#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/protocol.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

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

  private:
    std::string uri_;
    std::string path_;
    std::string text_;
    std::int32_t version_ = 0;

    // Cached parse results
    std::unique_ptr<frontend::TokenStream> tokens_;
    std::unique_ptr<frontend::Syntax> syntax_;
    std::vector<diagnostics::Diagnostic> diagnostics_;
};

// Manages all open documents
class DocumentManager {
  public:
    void open(TextDocumentItem item);
    void change(const VersionedTextDocumentIdentifier& id, const std::vector<TextDocumentContentChangeEvent>& changes);
    void close(const TextDocumentIdentifier& id);

    [[nodiscard]] Document* get(const std::string& uri);
    [[nodiscard]] const Document* get(const std::string& uri) const;

  private:
    std::unordered_map<std::string, std::unique_ptr<Document>> documents_;
};

} // namespace cppl::lsp
