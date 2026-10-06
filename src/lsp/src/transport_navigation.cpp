// Requests that lead from a name: definitions, references, highlights,
// symbols and renames.

#include "cppl/clang/editor.hpp"
#include "cppl/lsp/json.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/lsp/workspace_index.hpp"
#include "transport_dispatcher.hpp"

#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cppl::lsp {

using detail::transport::Dispatcher;
using detail::transport::kInvalidParams;
using detail::transport::kRequestFailed;
using detail::transport::parse_position;

std::optional<std::pair<TextDocumentIdentifier, Position>> Dispatcher::position_params(const json::Value* params) {
    const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
    const json::Value* position_value = params != nullptr ? params->find("position") : nullptr;
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    const std::optional<Position> position = position_value != nullptr ? parse_position(*position_value) : std::nullopt;
    if (!uri || !position.has_value()) {
        return std::nullopt;
    }
    TextDocumentIdentifier document_id;
    document_id.uri = *uri;
    return std::pair{document_id, *position};
}

json::Value Dispatcher::range_to_json(const Range& range) {
    json::Value start = json::Value::object();
    start.set("line", json::Value(range.start.line));
    start.set("character", json::Value(range.start.character));
    json::Value end = json::Value::object();
    end.set("line", json::Value(range.end.line));
    end.set("character", json::Value(range.end.character));
    json::Value result = json::Value::object();
    result.set("start", std::move(start));
    result.set("end", std::move(end));
    return result;
}

json::Value Dispatcher::location_to_json(const Location& location) {
    json::Value result = json::Value::object();
    result.set("uri", json::Value(location.uri));
    result.set("range", range_to_json(location.range));
    return result;
}

void Dispatcher::handle_navigate(const json::Value* id, const json::Value* params, clangbridge::Destination destination,
                                 const std::string& method) {
    if (id == nullptr) {
        return;
    }
    const auto request = position_params(params);
    if (!request.has_value()) {
        respond_error(*id, kInvalidParams, method + " missing 'textDocument.uri' or 'position'");
        return;
    }
    const std::optional<std::vector<Location>> locations =
        server_.text_document_navigate(destination, request->first, request->second);
    if (!locations.has_value()) {
        respond_result(*id, json::Value(nullptr));
        return;
    }
    json::Value items = json::Value::array();
    for (const Location& location : *locations) {
        items.push_back(location_to_json(location));
    }
    respond_result(*id, std::move(items));
}

void Dispatcher::handle_references(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const auto request = position_params(params);
    if (!request.has_value()) {
        respond_error(*id, kInvalidParams, "textDocument/references missing 'textDocument.uri' or 'position'");
        return;
    }
    // `context.includeDeclaration` is required by the protocol; a client
    // that leaves it out is answered as if it had asked for them.
    bool include_declaration = true;
    if (const json::Value* context = params->find("context")) {
        if (const json::Value* flag = context->find("includeDeclaration"); flag != nullptr && flag->is_boolean()) {
            include_declaration = flag->as_boolean();
        }
    }
    const std::optional<std::vector<Location>> locations =
        server_.text_document_references(request->first, request->second, include_declaration);
    if (!locations.has_value()) {
        respond_result(*id, json::Value(nullptr));
        return;
    }
    json::Value items = json::Value::array();
    for (const Location& location : *locations) {
        items.push_back(location_to_json(location));
    }
    respond_result(*id, std::move(items));
}

void Dispatcher::handle_document_highlight(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const auto request = position_params(params);
    if (!request.has_value()) {
        respond_error(*id, kInvalidParams, "textDocument/documentHighlight missing 'textDocument.uri' or 'position'");
        return;
    }
    const std::optional<std::vector<DocumentHighlight>> highlights =
        server_.text_document_document_highlight(request->first, request->second);
    if (!highlights.has_value()) {
        respond_result(*id, json::Value(nullptr));
        return;
    }
    json::Value items = json::Value::array();
    for (const DocumentHighlight& highlight : *highlights) {
        json::Value item = json::Value::object();
        item.set("range", range_to_json(highlight.range));
        item.set("kind", json::Value(static_cast<int>(highlight.kind)));
        items.push_back(std::move(item));
    }
    respond_result(*id, std::move(items));
}

json::Value Dispatcher::document_symbol_to_json(const DocumentSymbol& symbol) {
    json::Value result = json::Value::object();
    result.set("name", json::Value(symbol.name));
    if (!symbol.detail.empty()) {
        result.set("detail", json::Value(symbol.detail));
    }
    result.set("kind", json::Value(static_cast<int>(symbol.kind)));
    result.set("range", range_to_json(symbol.range));
    result.set("selectionRange", range_to_json(symbol.selection));
    if (!symbol.children.empty()) {
        json::Value children = json::Value::array();
        for (const DocumentSymbol& child : symbol.children) {
            children.push_back(document_symbol_to_json(child));
        }
        result.set("children", std::move(children));
    }
    return result;
}

void Dispatcher::flatten_symbols(const std::vector<DocumentSymbol>& symbols, const std::string& uri,
                                 const std::string& container, json::Value& into) {
    for (const DocumentSymbol& symbol : symbols) {
        json::Value item = json::Value::object();
        item.set("name", json::Value(symbol.name));
        item.set("kind", json::Value(static_cast<int>(symbol.kind)));
        item.set("location", location_to_json(Location{uri, symbol.range}));
        if (!container.empty()) {
            item.set("containerName", json::Value(container));
        }
        into.push_back(std::move(item));
        flatten_symbols(symbol.children, uri, container.empty() ? symbol.name : container + "::" + symbol.name, into);
    }
}

void Dispatcher::handle_document_symbol(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    if (!uri) {
        respond_error(*id, kInvalidParams, "textDocument/documentSymbol missing 'textDocument.uri'");
        return;
    }
    TextDocumentIdentifier document_id;
    document_id.uri = *uri;
    const std::optional<std::vector<DocumentSymbol>> symbols = server_.text_document_document_symbol(document_id);
    if (!symbols.has_value()) {
        respond_result(*id, json::Value(nullptr));
        return;
    }
    json::Value items = json::Value::array();
    if (server_.client_capabilities().hierarchical_symbols) {
        for (const DocumentSymbol& symbol : *symbols) {
            items.push_back(document_symbol_to_json(symbol));
        }
    } else {
        flatten_symbols(*symbols, *uri, std::string(), items);
    }
    respond_result(*id, std::move(items));
}

void Dispatcher::handle_workspace_symbol(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const std::string query = params != nullptr ? params->find_string("query").value_or(std::string()) : "";
    json::Value items = json::Value::array();
    for (const WorkspaceIndex::Symbol& symbol : server_.workspace_symbols(query)) {
        json::Value item = json::Value::object();
        item.set("name", json::Value(symbol.name));
        item.set("kind", json::Value(static_cast<int>(symbol.kind)));
        item.set("location", location_to_json(symbol.location));
        if (!symbol.container.empty()) {
            item.set("containerName", json::Value(symbol.container));
        }
        items.push_back(std::move(item));
    }
    respond_result(*id, std::move(items));
}

void Dispatcher::handle_prepare_rename(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const auto request = position_params(params);
    if (!request.has_value()) {
        respond_error(*id, kInvalidParams, "textDocument/prepareRename missing 'textDocument.uri' or 'position'");
        return;
    }
    const std::expected<PrepareRename, std::string> prepared =
        server_.text_document_prepare_rename(request->first, request->second);
    if (!prepared.has_value()) {
        respond_error(*id, kRequestFailed, prepared.error());
        return;
    }
    json::Value result = json::Value::object();
    result.set("range", range_to_json(prepared->range));
    result.set("placeholder", json::Value(prepared->placeholder));
    respond_result(*id, std::move(result));
}

void Dispatcher::handle_rename(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const auto request = position_params(params);
    const std::optional<std::string> new_name =
        params != nullptr ? params->find_string("newName") : std::optional<std::string>();
    if (!request.has_value() || !new_name.has_value()) {
        respond_error(*id, kInvalidParams, "textDocument/rename missing 'textDocument.uri', 'position' or 'newName'");
        return;
    }
    const std::expected<WorkspaceEdit, std::string> edit =
        server_.text_document_rename(request->first, request->second, *new_name);
    if (!edit.has_value()) {
        respond_error(*id, kRequestFailed, edit.error());
        return;
    }
    json::Value changes = json::Value::object();
    for (const auto& [uri, edits] : edit->changes) {
        changes.set(uri, text_edits_to_json(edits));
    }
    json::Value result = json::Value::object();
    result.set("changes", std::move(changes));
    respond_result(*id, std::move(result));
}

} // namespace cppl::lsp
