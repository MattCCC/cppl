#include "cppl/lsp/document.hpp"

#include "cppl/frontend/token.hpp"
#include "cppl/lsp/uri.hpp"

namespace cppl::lsp {

Document::Document(std::string uri, std::string text, std::int32_t version)
    : uri_(std::move(uri)),
      text_(std::move(text)),
      version_(version) {
    reparse();
}

void Document::update(std::string text, std::int32_t version) {
    text_ = std::move(text);
    version_ = version;
    reparse();
}

void Document::apply_change(const TextDocumentContentChangeEvent& change, std::int32_t version) {
    if (!change.range.has_value()) {
        // Full document sync
        update(change.text, version);
        return;
    }

    // Incremental update (not yet implemented)
    // For now, reject incremental changes
    // This should not be called if we advertise Full sync
    update(change.text, version);
}

void Document::reparse() {
    diagnostics_.clear();

    // Resolve the URI to a native filesystem path (cross-platform, percent
    // decoded) rather than a naive "file://" substr, so diagnostics and
    // #include resolution see a realistic path. A URI this server did not
    // itself accept (e.g. a non-file scheme) falls back to the raw URI text,
    // which still gives stable, if unresolved, diagnostics.
    path_ = uri_to_path(uri_).value_or(uri_);

    tokens_ = std::make_unique<frontend::TokenStream>(frontend::lex(text_, path_));

    // Parse C++L constructs
    diagnostics::Engine engine;
    auto syntax = frontend::recognize(*tokens_, engine);
    syntax_ = std::make_unique<frontend::Syntax>(std::move(syntax));

    // Store diagnostics from parsing
    diagnostics_ = engine.diagnostics();
}

void DocumentManager::open(TextDocumentItem item) {
    auto doc = std::make_unique<Document>(item.uri, std::move(item.text), item.version);
    documents_[item.uri] = std::move(doc);
}

void DocumentManager::change(const VersionedTextDocumentIdentifier& id,
                             const std::vector<TextDocumentContentChangeEvent>& changes) {
    auto it = documents_.find(id.uri);
    if (it == documents_.end()) {
        return;
    }

    for (auto& change : changes) {
        it->second->apply_change(change, id.version);
    }
}

void DocumentManager::close(const TextDocumentIdentifier& id) {
    documents_.erase(id.uri);
}

Document* DocumentManager::get(const std::string& uri) {
    auto it = documents_.find(uri);
    return it != documents_.end() ? it->second.get() : nullptr;
}

const Document* DocumentManager::get(const std::string& uri) const {
    auto it = documents_.find(uri);
    return it != documents_.end() ? it->second.get() : nullptr;
}

} // namespace cppl::lsp
