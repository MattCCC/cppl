// Requests that present a document: folding ranges, inlay hints, selection
// ranges, code lenses and semantic tokens.

#include "cppl/lsp/json.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "transport_dispatcher.hpp"

#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace cppl::lsp {

using detail::transport::Dispatcher;
using detail::transport::kInvalidParams;
using detail::transport::parse_position;
using detail::transport::parse_range;

void Dispatcher::handle_folding_range(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    if (!uri) {
        respond_error(*id, kInvalidParams, "textDocument/foldingRange missing 'textDocument.uri'");
        return;
    }
    TextDocumentIdentifier document_id;
    document_id.uri = *uri;
    const std::optional<std::vector<FoldingRange>> folds = server_.text_document_folding_range(document_id);
    if (!folds.has_value()) {
        respond_result(*id, json::Value(nullptr));
        return;
    }
    json::Value items = json::Value::array();
    for (const FoldingRange& fold : *folds) {
        json::Value item = json::Value::object();
        item.set("startLine", json::Value(fold.start_line));
        if (fold.start_character.has_value()) {
            item.set("startCharacter", json::Value(*fold.start_character));
        }
        item.set("endLine", json::Value(fold.end_line));
        if (fold.end_character.has_value()) {
            item.set("endCharacter", json::Value(*fold.end_character));
        }
        if (!fold.kind.empty()) {
            item.set("kind", json::Value(fold.kind));
        }
        items.push_back(std::move(item));
    }
    respond_result(*id, std::move(items));
}

void Dispatcher::handle_inlay_hint(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    const json::Value* range_value = params != nullptr ? params->find("range") : nullptr;
    const std::optional<Range> range = range_value != nullptr ? parse_range(*range_value) : std::nullopt;
    if (!uri || !range) {
        respond_error(*id, kInvalidParams, "textDocument/inlayHint needs 'textDocument.uri' and a well-formed 'range'");
        return;
    }
    TextDocumentIdentifier document_id;
    document_id.uri = *uri;
    const std::optional<std::vector<InlayHint>> hints = server_.text_document_inlay_hint(document_id, *range);
    if (!hints.has_value()) {
        respond_result(*id, json::Value(nullptr));
        return;
    }
    json::Value items = json::Value::array();
    for (const InlayHint& hint : *hints) {
        json::Value item = json::Value::object();
        json::Value position = json::Value::object();
        position.set("line", json::Value(hint.position.line));
        position.set("character", json::Value(hint.position.character));
        item.set("position", std::move(position));
        item.set("label", json::Value(hint.label));
        item.set("kind", json::Value(static_cast<int>(hint.kind)));
        // A parameter's name reads `name: argument`; a type `name: type`.
        if (hint.kind == InlayHintKind::Parameter) {
            item.set("paddingRight", json::Value(true));
        }
        items.push_back(std::move(item));
    }
    respond_result(*id, std::move(items));
}

void Dispatcher::handle_selection_range(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    const json::Value* listed = params != nullptr ? params->find("positions") : nullptr;
    if (!uri || listed == nullptr || !listed->is_array()) {
        respond_error(*id, kInvalidParams,
                      "textDocument/selectionRange needs 'textDocument.uri' and an array of 'positions'");
        return;
    }
    if (listed->as_array().size() > kMaxSelectionPositions) {
        respond_error(*id, kInvalidParams, "textDocument/selectionRange names more positions than any editor has");
        return;
    }
    std::vector<Position> positions;
    for (const json::Value& value : listed->as_array()) {
        const std::optional<Position> position = parse_position(value);
        if (!position.has_value()) {
            respond_error(*id, kInvalidParams, "textDocument/selectionRange has a malformed position");
            return;
        }
        positions.push_back(*position);
    }
    TextDocumentIdentifier document_id;
    document_id.uri = *uri;
    const std::optional<std::vector<std::vector<Range>>> chains =
        server_.text_document_selection_range(document_id, positions);
    if (!chains.has_value()) {
        respond_result(*id, json::Value(nullptr));
        return;
    }
    json::Value items = json::Value::array();
    for (const std::vector<Range>& chain : *chains) {
        // Nested from the outermost in: each range's parent holds it.
        json::Value selection(nullptr);
        for (const Range& range : std::views::reverse(chain)) {
            json::Value inner = json::Value::object();
            inner.set("range", range_to_json(range));
            if (!selection.is_null()) {
                inner.set("parent", std::move(selection));
            }
            selection = std::move(inner);
        }
        items.push_back(std::move(selection));
    }
    respond_result(*id, std::move(items));
}

void Dispatcher::handle_code_lens(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    if (!uri) {
        respond_error(*id, kInvalidParams, "textDocument/codeLens missing 'textDocument.uri'");
        return;
    }
    TextDocumentIdentifier document_id;
    document_id.uri = *uri;
    const std::optional<std::vector<CodeLens>> lenses = server_.text_document_code_lens(document_id);
    if (!lenses.has_value()) {
        respond_result(*id, json::Value(nullptr));
        return;
    }
    json::Value items = json::Value::array();
    for (const CodeLens& lens : *lenses) {
        // An empty command is a statement, not an action: an editor shows
        // the title and runs nothing.
        json::Value command = json::Value::object();
        command.set("title", json::Value(lens.title));
        command.set("command", json::Value(std::string()));
        json::Value item = json::Value::object();
        item.set("range", range_to_json(lens.range));
        item.set("command", std::move(command));
        items.push_back(std::move(item));
    }
    respond_result(*id, std::move(items));
}

void Dispatcher::handle_semantic_tokens(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    if (!uri) {
        respond_error(*id, kInvalidParams, "textDocument/semanticTokens/full missing 'textDocument.uri'");
        return;
    }
    TextDocumentIdentifier document_id;
    document_id.uri = *uri;
    const std::optional<std::vector<std::uint32_t>> tokens = server_.text_document_semantic_tokens(document_id);
    if (!tokens.has_value()) {
        respond_result(*id, json::Value(nullptr));
        return;
    }
    json::Value data = json::Value::array();
    for (const std::uint32_t value : *tokens) {
        data.push_back(json::Value(value));
    }
    json::Value result = json::Value::object();
    result.set("data", std::move(data));
    respond_result(*id, std::move(result));
}

} // namespace cppl::lsp
