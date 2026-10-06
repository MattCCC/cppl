// Requests that help write code: completion, hover, signature help,
// formatting and code actions.

#include "cppl/lsp/json.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "transport_dispatcher.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cppl::lsp {

using detail::transport::Dispatcher;
using detail::transport::kInvalidParams;
using detail::transport::parse_position;
using detail::transport::parse_range;

json::Value Dispatcher::text_edits_to_json(const std::vector<TextEdit>& edits) {
    json::Value items = json::Value::array();
    for (const TextEdit& edit : edits) {
        json::Value range = json::Value::object();
        json::Value start = json::Value::object();
        start.set("line", json::Value(edit.range.start.line));
        start.set("character", json::Value(edit.range.start.character));
        json::Value end = json::Value::object();
        end.set("line", json::Value(edit.range.end.line));
        end.set("character", json::Value(edit.range.end.character));
        range.set("start", start);
        range.set("end", end);

        json::Value item = json::Value::object();
        item.set("range", range);
        item.set("newText", json::Value(edit.newText));
        items.push_back(item);
    }
    return items;
}

void Dispatcher::respond_edits(const json::Value& id, std::optional<std::vector<TextEdit>> edits) {
    if (!edits.has_value()) {
        respond_result(id, json::Value(nullptr));
        return;
    }
    respond_result(id, text_edits_to_json(*edits));
}

void Dispatcher::handle_formatting(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return; // a notification would be malformed per the LSP spec; nothing to respond to
    }
    const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    if (!uri) {
        respond_error(*id, kInvalidParams, "textDocument/formatting missing 'textDocument.uri'");
        return;
    }
    TextDocumentIdentifier document_id;
    document_id.uri = *uri;
    respond_edits(*id, server_.text_document_formatting(document_id));
}

void Dispatcher::handle_completion(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
    const json::Value* position_value = params != nullptr ? params->find("position") : nullptr;
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    const std::optional<Position> position = position_value != nullptr ? parse_position(*position_value) : std::nullopt;
    if (!uri || !position.has_value()) {
        respond_error(*id, kInvalidParams, "textDocument/completion missing 'textDocument.uri' or 'position'");
        return;
    }
    TextDocumentIdentifier document_id;
    document_id.uri = *uri;

    const CompletionList completion = server_.text_document_completion(document_id, *position);
    json::Value items = json::Value::array();
    for (const CompletionItem& item : completion.items) {
        json::Value entry = json::Value::object();
        entry.set("label", json::Value(item.label));
        entry.set("kind", json::Value(static_cast<int>(item.kind)));
        if (!item.detail.empty()) {
            entry.set("detail", json::Value(item.detail));
        }
        if (!item.documentation.empty()) {
            entry.set("documentation", json::Value(item.documentation));
        }
        if (!item.insertText.empty()) {
            entry.set("insertText", json::Value(item.insertText));
        }
        if (item.snippet) {
            constexpr int kSnippet = 2; // InsertTextFormat.Snippet
            entry.set("insertTextFormat", json::Value(kSnippet));
        }
        if (!item.filterText.empty()) {
            entry.set("filterText", json::Value(item.filterText));
        }
        if (!item.sortText.empty()) {
            entry.set("sortText", json::Value(item.sortText));
        }
        if (item.deprecated) {
            json::Value tags = json::Value::array();
            tags.push_back(json::Value(1)); // CompletionItemTag.Deprecated
            entry.set("tags", std::move(tags));
        }
        items.push_back(std::move(entry));
    }
    // An incomplete list is asked for again as the user types: more
    // matched than was sent.
    json::Value list = json::Value::object();
    list.set("isIncomplete", json::Value(completion.incomplete));
    list.set("items", std::move(items));
    respond_result(*id, std::move(list));
}

void Dispatcher::handle_hover(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
    const json::Value* position_value = params != nullptr ? params->find("position") : nullptr;
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    const std::optional<Position> position = position_value != nullptr ? parse_position(*position_value) : std::nullopt;
    if (!uri || !position.has_value()) {
        respond_error(*id, kInvalidParams, "textDocument/hover missing 'textDocument.uri' or 'position'");
        return;
    }
    TextDocumentIdentifier document_id;
    document_id.uri = *uri;

    const std::optional<Hover> hover = server_.text_document_hover(document_id, *position);
    if (!hover.has_value()) {
        respond_result(*id, json::Value(nullptr));
        return;
    }
    json::Value contents = json::Value::object();
    contents.set("kind", json::Value(std::string("markdown")));
    contents.set("value", json::Value(hover->contents));
    json::Value result = json::Value::object();
    result.set("contents", std::move(contents));
    if (hover->range.has_value()) {
        result.set("range", range_to_json(*hover->range));
    }
    respond_result(*id, std::move(result));
}

void Dispatcher::handle_signature_help(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const auto request = position_params(params);
    if (!request.has_value()) {
        respond_error(*id, kInvalidParams, "textDocument/signatureHelp missing 'textDocument.uri' or 'position'");
        return;
    }
    const std::optional<SignatureHelp> help = server_.text_document_signature_help(request->first, request->second);
    if (!help.has_value()) {
        respond_result(*id, json::Value(nullptr));
        return;
    }
    json::Value signatures = json::Value::array();
    for (const SignatureInformation& signature : help->signatures) {
        json::Value entry = json::Value::object();
        entry.set("label", json::Value(signature.label));
        if (!signature.documentation.empty()) {
            entry.set("documentation", json::Value(signature.documentation));
        }
        json::Value parameters = json::Value::array();
        for (const auto& [start, end] : signature.parameters) {
            json::Value label = json::Value::array();
            label.push_back(json::Value(start));
            label.push_back(json::Value(end));
            json::Value parameter = json::Value::object();
            parameter.set("label", std::move(label));
            parameters.push_back(std::move(parameter));
        }
        entry.set("parameters", std::move(parameters));
        signatures.push_back(std::move(entry));
    }
    json::Value result = json::Value::object();
    result.set("signatures", std::move(signatures));
    result.set("activeSignature", json::Value(help->active_signature));
    result.set("activeParameter", json::Value(help->active_parameter));
    respond_result(*id, std::move(result));
}

void Dispatcher::handle_code_action(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
    const json::Value* range_value = params != nullptr ? params->find("range") : nullptr;
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    const std::optional<Range> range = range_value != nullptr ? parse_range(*range_value) : std::nullopt;
    if (!uri || !range.has_value()) {
        respond_error(*id, kInvalidParams, "textDocument/codeAction missing 'textDocument.uri' or 'range'");
        return;
    }
    CodeActionRequest request;
    request.document.uri = *uri;
    request.range = *range;
    if (const json::Value* context = params->find("context")) {
        if (const json::Value* only = context->find("only"); only != nullptr && only->is_array()) {
            for (const json::Value& kind : only->as_array()) {
                if (kind.is_string()) {
                    request.only.push_back(kind.as_string());
                }
            }
        }
        constexpr double kAutomatic = 2.0; // CodeActionTriggerKind.Automatic
        request.automatic = context->find_number("triggerKind") == kAutomatic;
    }

    json::Value actions = json::Value::array();
    for (const CodeAction& action : server_.text_document_code_actions(request)) {
        json::Value changes = json::Value::object();
        changes.set(*uri, text_edits_to_json(action.edits));
        json::Value edit = json::Value::object();
        edit.set("changes", std::move(changes));
        json::Value entry = json::Value::object();
        entry.set("title", json::Value(action.title));
        entry.set("kind", json::Value(action.kind));
        entry.set("edit", std::move(edit));
        actions.push_back(std::move(entry));
    }
    respond_result(*id, std::move(actions));
}

void Dispatcher::handle_range_formatting(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
    const json::Value* range_value = params != nullptr ? params->find("range") : nullptr;
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    const std::optional<Range> range = range_value != nullptr ? parse_range(*range_value) : std::nullopt;
    if (!uri || !range.has_value()) {
        respond_error(*id, kInvalidParams, "textDocument/rangeFormatting missing 'textDocument.uri' or 'range'");
        return;
    }
    TextDocumentIdentifier document_id;
    document_id.uri = *uri;
    respond_edits(*id, server_.text_document_range_formatting(document_id, *range));
}

void Dispatcher::handle_on_type_formatting(const json::Value* id, const json::Value* params) {
    if (id == nullptr) {
        return;
    }
    const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
    const json::Value* position_value = params != nullptr ? params->find("position") : nullptr;
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    const auto character = params != nullptr ? params->find_string("ch") : std::nullopt;
    if (!uri || position_value == nullptr || !character) {
        respond_error(*id, kInvalidParams,
                      "textDocument/onTypeFormatting missing 'textDocument.uri', 'position' or 'ch'");
        return;
    }
    const std::optional<Position> position = parse_position(*position_value);
    if (!position) {
        respond_error(*id, kInvalidParams, "textDocument/onTypeFormatting has a malformed 'position'");
        return;
    }

    TextDocumentIdentifier document_id;
    document_id.uri = *uri;
    respond_edits(*id, server_.text_document_on_type_formatting(document_id, *position, *character));
}

} // namespace cppl::lsp
