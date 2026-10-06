// The dispatcher's routing of each message, the session's lifecycle, document
// synchronization, and the diagnostics it publishes.

#include "cppl/clang/editor.hpp"
#include "cppl/lsp/json.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/semantic_tokens.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/lsp/uri.hpp"
#include "transport_dispatcher.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::lsp {

using detail::transport::Dispatcher;
using detail::transport::parse_range;
using detail::transport::request;

namespace {

json::Value make_error(int code, const std::string& message) {
    json::Value error = json::Value::object();
    error.set("code", json::Value(code));
    error.set("message", json::Value(message));
    return error;
}

constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;

constexpr int kInvalidRequestState = -32600; // reused for "server is shutting down"

json::Value diagnostic_to_json(const Diagnostic& diagnostic) {
    json::Value range = json::Value::object();
    json::Value start = json::Value::object();
    start.set("line", json::Value(diagnostic.range.start.line));
    start.set("character", json::Value(diagnostic.range.start.character));
    json::Value end = json::Value::object();
    end.set("line", json::Value(diagnostic.range.end.line));
    end.set("character", json::Value(diagnostic.range.end.character));
    range.set("start", start);
    range.set("end", end);

    json::Value out = json::Value::object();
    out.set("range", range);
    out.set("severity", json::Value(static_cast<int>(diagnostic.severity)));
    out.set("code", json::Value(diagnostic.code));
    out.set("source", json::Value(diagnostic.source));
    out.set("message", json::Value(diagnostic.message));

    if (!diagnostic.relatedInformation.empty()) {
        json::Value related = json::Value::array();
        for (const auto& info : diagnostic.relatedInformation) {
            json::Value location = json::Value::object();
            location.set("uri", json::Value(info.location.uri));
            json::Value info_range = json::Value::object();
            json::Value info_start = json::Value::object();
            info_start.set("line", json::Value(info.location.range.start.line));
            info_start.set("character", json::Value(info.location.range.start.character));
            json::Value info_end = json::Value::object();
            info_end.set("line", json::Value(info.location.range.end.line));
            info_end.set("character", json::Value(info.location.range.end.character));
            info_range.set("start", info_start);
            info_range.set("end", info_end);
            location.set("range", info_range);

            json::Value entry = json::Value::object();
            entry.set("location", location);
            entry.set("message", json::Value(info.message));
            related.push_back(entry);
        }
        out.set("relatedInformation", related);
    }
    return out;
}

} // namespace

namespace detail::transport {

// An LSP position is a pair of non-negative UTF-16 offsets. They arrive from an
// untrusted client as JSON doubles, and narrowing a negative or out-of-range
// double to `std::uint32_t` is undefined behavior, so the range is checked
// before the conversion rather than after it.
std::optional<Position> parse_position(const json::Value& value) {
    const auto line = value.find_number("line");
    const auto character = value.find_number("character");
    if (!line || !character) {
        return std::nullopt;
    }
    constexpr double limit = 4294967295.0; // std::uint32_t's maximum, exactly representable
    const auto in_range = [](double number) {
        // NaN fails every comparison, so it is out of range here too.
        return number >= 0.0 && number <= limit;
    };
    if (!in_range(*line) || !in_range(*character)) {
        // Rejected as malformed params rather than clamped, which would answer
        // for a position the client never asked about.
        return std::nullopt;
    }
    Position position;
    position.line = static_cast<std::uint32_t>(*line);
    position.character = static_cast<std::uint32_t>(*character);
    return position;
}

// Both ends are checked exactly as `parse_position` checks one, and an end
// before its start names no text, so that is malformed too.
std::optional<Range> parse_range(const json::Value& value) {
    const json::Value* start = value.find("start");
    const json::Value* end = value.find("end");
    if (start == nullptr || end == nullptr) {
        return std::nullopt;
    }
    const std::optional<Position> from = parse_position(*start);
    const std::optional<Position> to = parse_position(*end);
    if (!from || !to) {
        return std::nullopt;
    }
    if (to->line < from->line || (to->line == from->line && to->character < from->character)) {
        return std::nullopt;
    }
    return Range{*from, *to};
}

} // namespace detail::transport

namespace {

// A document version is an integer that arrives as a JSON double, and
// narrowing a double outside `std::int32_t` is undefined behavior exactly as it
// is for a position.
std::optional<std::int32_t> parse_version(double number) {
    constexpr double lowest = -2147483648.0;
    constexpr double highest = 2147483647.0;
    // NaN fails every comparison, so it is out of range here too.
    const bool in_range = number >= lowest && number <= highest;
    if (!in_range) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(number);
}

constexpr int kRequestCancelled = -32800;

} // namespace

namespace detail::transport {

json::Value request(const std::string& id, const std::string& method, json::Value params) {
    json::Value message = json::Value::object();
    message.set("jsonrpc", json::Value("2.0"));
    message.set("id", json::Value(id));
    message.set("method", json::Value(method));
    message.set("params", std::move(params));
    return message;
}

} // namespace detail::transport

void Dispatcher::request_progress_token() {
    if (tokens_ == nullptr || !server_.client_capabilities().work_done_progress) {
        return;
    }
    const std::string token = "cppl/progress/" + std::to_string(++sent_);
    const std::string id = "cppl/create/" + std::to_string(sent_);
    pending_tokens_.emplace(id, token);
    json::Value params = json::Value::object();
    params.set("token", json::Value(token));
    writer_.write(request(id, "window/workDoneProgress/create", std::move(params)));
}

void Dispatcher::refresh_after_compile() {
    const ClientCapabilities& client = server_.client_capabilities();
    if (client.code_lens_refresh) {
        writer_.write(
            request("cppl/refresh/" + std::to_string(++sent_), "workspace/codeLens/refresh", json::Value(nullptr)));
    }
    if (client.semantic_tokens_refresh) {
        writer_.write(request("cppl/refresh/" + std::to_string(++sent_), "workspace/semanticTokens/refresh",
                              json::Value(nullptr)));
    }
}

void Dispatcher::cancelled(const json::Value& id) {
    respond_error(id, kRequestCancelled, "the request was cancelled");
}

bool Dispatcher::dispatch(const json::Value& message) {
    const json::Value* method_value = message.find("method");
    const json::Value* id_value = message.find("id");
    const bool is_request = id_value != nullptr;

    // The client's answer to a request this server sent.
    if (method_value == nullptr && id_value != nullptr &&
        (message.find("result") != nullptr || message.find("error") != nullptr)) {
        answered(*id_value, message.find("error") == nullptr);
        return true;
    }

    if (method_value == nullptr || !method_value->is_string()) {
        if (is_request) {
            respond_error(*id_value, kInvalidRequest, "missing or invalid 'method'");
        } else {
            log_ << "cppl-lsp: dropping a message with no method\n";
        }
        return true;
    }
    const std::string& method = method_value->as_string();
    const json::Value* params = message.find("params");

    if (shutting_down_ && method != "exit") {
        // Per the LSP spec: after shutdown, every request other than
        // exit must be rejected (not just ignored), and notifications
        // other than exit are dropped.
        if (is_request) {
            respond_error(*id_value, kInvalidRequestState, "server is shutting down");
        }
        return true;
    }

    if (method == "initialize") {
        handle_initialize(id_value, params);
    } else if (method == "initialized") {
        server_.initialized();
        // One token for compiles, and one for indexing while it runs.
        request_progress_token();
        if (server_.indexing()) {
            request_progress_token();
        }
    } else if (method.starts_with("$/")) {
        // `$/cancelRequest` for a request already answered, and any
        // other protocol-internal notification, which may be ignored.
    } else if (method == "shutdown") {
        server_.shutdown();
        shutting_down_ = true;
        if (is_request) {
            respond_result(*id_value, json::Value(nullptr));
        }
    } else if (method == "exit") {
        server_.exit();
        return false;
    } else if (method == "textDocument/didOpen") {
        handle_did_open(params);
    } else if (method == "textDocument/didChange") {
        handle_did_change(params);
    } else if (method == "textDocument/didClose") {
        handle_did_close(params);
    } else if (method == "textDocument/formatting") {
        handle_formatting(id_value, params);
    } else if (method == "textDocument/rangeFormatting") {
        handle_range_formatting(id_value, params);
    } else if (method == "textDocument/onTypeFormatting") {
        handle_on_type_formatting(id_value, params);
    } else if (method == "textDocument/completion") {
        handle_completion(id_value, params);
    } else if (method == "textDocument/hover") {
        handle_hover(id_value, params);
    } else if (method == "textDocument/codeAction") {
        handle_code_action(id_value, params);
    } else if (method == "textDocument/semanticTokens/full") {
        handle_semantic_tokens(id_value, params);
    } else if (method == "textDocument/definition") {
        handle_navigate(id_value, params, clangbridge::Destination::Definition, method);
    } else if (method == "textDocument/declaration") {
        handle_navigate(id_value, params, clangbridge::Destination::Declaration, method);
    } else if (method == "textDocument/typeDefinition") {
        handle_navigate(id_value, params, clangbridge::Destination::TypeDefinition, method);
    } else if (method == "textDocument/implementation") {
        handle_navigate(id_value, params, clangbridge::Destination::Implementation, method);
    } else if (method == "textDocument/references") {
        handle_references(id_value, params);
    } else if (method == "textDocument/documentHighlight") {
        handle_document_highlight(id_value, params);
    } else if (method == "textDocument/codeLens") {
        handle_code_lens(id_value, params);
    } else if (method == "textDocument/signatureHelp") {
        handle_signature_help(id_value, params);
    } else if (method == "textDocument/documentSymbol") {
        handle_document_symbol(id_value, params);
    } else if (method == "textDocument/foldingRange") {
        handle_folding_range(id_value, params);
    } else if (method == "textDocument/selectionRange") {
        handle_selection_range(id_value, params);
    } else if (method == "textDocument/inlayHint") {
        handle_inlay_hint(id_value, params);
    } else if (method == "workspace/symbol") {
        handle_workspace_symbol(id_value, params);
    } else if (method == "textDocument/prepareRename") {
        handle_prepare_rename(id_value, params);
    } else if (method == "textDocument/rename") {
        handle_rename(id_value, params);
    } else if (is_request) {
        respond_error(*id_value, kMethodNotFound, "method not found: " + method);
    } else {
        // An unknown notification is silently ignored per the JSON-RPC
        // / LSP spec: it is not an error, and there is nothing to
        // respond to since notifications never get a response.
        log_ << "cppl-lsp: ignoring unknown notification '" << method << "'\n";
    }
    return true;
}

bool Dispatcher::exited() const noexcept {
    return exited_;
}

void Dispatcher::write(const json::Value& message) {
    writer_.write(message);
}

void Dispatcher::answered(const json::Value& id, bool succeeded) {
    if (!id.is_string()) {
        return;
    }
    const auto pending = pending_tokens_.find(id.as_string());
    if (pending == pending_tokens_.end()) {
        return;
    }
    if (succeeded && tokens_ != nullptr) {
        tokens_->add(pending->second);
    }
    pending_tokens_.erase(pending);
}

void Dispatcher::respond_result(const json::Value& id, json::Value result) {
    json::Value message = json::Value::object();
    message.set("jsonrpc", json::Value("2.0"));
    message.set("id", id);
    message.set("result", std::move(result));
    write(message);
}

void Dispatcher::respond_error(const json::Value& id, int code, const std::string& text) {
    json::Value message = json::Value::object();
    message.set("jsonrpc", json::Value("2.0"));
    message.set("id", id);
    message.set("error", make_error(code, text));
    write(message);
}

ClientCapabilities Dispatcher::client_capabilities(const json::Value* params) {
    ClientCapabilities capabilities;
    const json::Value* stated = params != nullptr ? params->find("capabilities") : nullptr;
    const json::Value* document = stated != nullptr ? stated->find("textDocument") : nullptr;
    const json::Value* completion = document != nullptr ? document->find("completion") : nullptr;
    const json::Value* item = completion != nullptr ? completion->find("completionItem") : nullptr;
    if (const json::Value* snippets = item != nullptr ? item->find("snippetSupport") : nullptr;
        snippets != nullptr && snippets->is_boolean()) {
        capabilities.snippets = snippets->as_boolean();
    }
    const json::Value* symbols = document != nullptr ? document->find("documentSymbol") : nullptr;
    if (const json::Value* nested = symbols != nullptr ? symbols->find("hierarchicalDocumentSymbolSupport") : nullptr;
        nested != nullptr && nested->is_boolean()) {
        capabilities.hierarchical_symbols = nested->as_boolean();
    }
    const json::Value* folding = document != nullptr ? document->find("foldingRange") : nullptr;
    if (const json::Value* lines = folding != nullptr ? folding->find("lineFoldingOnly") : nullptr;
        lines != nullptr && lines->is_boolean()) {
        capabilities.line_folding_only = lines->as_boolean();
    }
    const auto flag = [](const json::Value* section, const char* name) {
        const json::Value* value = section != nullptr ? section->find(name) : nullptr;
        return value != nullptr && value->is_boolean() && value->as_boolean();
    };
    capabilities.work_done_progress = flag(stated != nullptr ? stated->find("window") : nullptr, "workDoneProgress");
    const json::Value* workspace = stated != nullptr ? stated->find("workspace") : nullptr;
    capabilities.code_lens_refresh =
        flag(workspace != nullptr ? workspace->find("codeLens") : nullptr, "refreshSupport");
    capabilities.semantic_tokens_refresh =
        flag(workspace != nullptr ? workspace->find("semanticTokens") : nullptr, "refreshSupport");
    capabilities.prepare_rename = flag(document != nullptr ? document->find("rename") : nullptr, "prepareSupport");
    return capabilities;
}

std::vector<std::string> Dispatcher::workspace_roots(const json::Value* params) {
    std::vector<std::string> roots;
    if (params == nullptr) {
        return roots;
    }
    const auto add = [&roots](const std::string& uri) {
        if (std::optional<std::string> path = uri_to_path(uri)) {
            roots.push_back(std::move(*path));
        }
    };
    if (const json::Value* folders = params->find("workspaceFolders"); folders != nullptr && folders->is_array()) {
        for (const json::Value& folder : folders->as_array()) {
            if (const std::optional<std::string> uri = folder.find_string("uri")) {
                add(*uri);
            }
        }
    }
    if (!roots.empty()) {
        return roots;
    }
    if (const std::optional<std::string> uri = params->find_string("rootUri")) {
        add(*uri);
    } else if (std::optional<std::string> path = params->find_string("rootPath")) {
        roots.push_back(std::move(*path));
    }
    return roots;
}

void Dispatcher::handle_initialize(const json::Value* id, const json::Value* params) {
    server_.initialize(client_capabilities(params), workspace_roots(params));

    json::Value capabilities = json::Value::object();
    // Incremental sync: a client sends only what changed, and each change
    // is applied where its range lands (Document::apply_changes).
    capabilities.set("textDocumentSync", json::Value(static_cast<int>(server_.sync_kind())));

    capabilities.set("documentFormattingProvider", json::Value(true));
    capabilities.set("documentRangeFormattingProvider", json::Value(true));

    json::Value on_type_formatting = json::Value::object();
    on_type_formatting.set("firstTriggerCharacter", json::Value(std::string("}")));
    json::Value more_trigger_characters = json::Value::array();
    more_trigger_characters.push_back(json::Value(std::string(";")));
    on_type_formatting.set("moreTriggerCharacter", more_trigger_characters);
    capabilities.set("documentOnTypeFormattingProvider", on_type_formatting);

    // Completion: Clang's for C++, after member access and scope as well
    // as while a name is typed, C++L's own words where the grammar admits
    // them, and inside a `cases`/`decompose` block the arms still owed.
    json::Value completion = json::Value::object();
    json::Value trigger_characters = json::Value::array();
    for (const char* trigger : {"{", ".", ">", ":"}) {
        trigger_characters.push_back(json::Value(std::string(trigger)));
    }
    completion.set("triggerCharacters", std::move(trigger_characters));
    capabilities.set("completionProvider", std::move(completion));
    capabilities.set("hoverProvider", json::Value(true));

    // The declarations a call being written could resolve to, from Clang.
    json::Value signature_help = json::Value::object();
    json::Value signature_triggers = json::Value::array();
    signature_triggers.push_back(json::Value(std::string("(")));
    signature_triggers.push_back(json::Value(std::string(",")));
    signature_help.set("triggerCharacters", std::move(signature_triggers));
    json::Value signature_retriggers = json::Value::array();
    signature_retriggers.push_back(json::Value(std::string(")")));
    signature_help.set("retriggerCharacters", std::move(signature_retriggers));
    capabilities.set("signatureHelpProvider", std::move(signature_help));

    // Syntax migrations as quick fixes, and canonical formatting as a
    // fix-all an editor can run on save.
    json::Value code_action_kinds = json::Value::array();
    code_action_kinds.push_back(json::Value(std::string(kQuickFixKind)));
    code_action_kinds.push_back(json::Value(std::string(kFixAllKind)));
    json::Value code_actions = json::Value::object();
    code_actions.set("codeActionKinds", std::move(code_action_kinds));
    capabilities.set("codeActionProvider", std::move(code_actions));

    // Every name as the kind of thing it names, from Clang, and C++L's
    // words and names from the recognizer (semantic_tokens.hpp).
    json::Value token_types = json::Value::array();
    for (const std::string_view type : kTokenTypes) {
        token_types.push_back(json::Value(std::string(type)));
    }
    json::Value token_modifiers = json::Value::array();
    for (const std::string_view modifier : kTokenModifiers) {
        token_modifiers.push_back(json::Value(std::string(modifier)));
    }
    json::Value legend = json::Value::object();
    legend.set("tokenTypes", std::move(token_types));
    legend.set("tokenModifiers", std::move(token_modifiers));
    json::Value semantic_tokens = json::Value::object();
    semantic_tokens.set("legend", std::move(legend));
    semantic_tokens.set("full", json::Value(true));
    capabilities.set("semanticTokensProvider", std::move(semantic_tokens));

    // Navigation over ordinary C++ and the C++L declarations the
    // projection stands for, answered by Clang.
    capabilities.set("definitionProvider", json::Value(true));
    capabilities.set("declarationProvider", json::Value(true));
    capabilities.set("typeDefinitionProvider", json::Value(true));
    capabilities.set("implementationProvider", json::Value(true));
    capabilities.set("referencesProvider", json::Value(true));
    capabilities.set("documentHighlightProvider", json::Value(true));
    json::Value document_symbols = json::Value::object();
    document_symbols.set("label", json::Value(std::string("C++L")));
    capabilities.set("documentSymbolProvider", std::move(document_symbols));
    // Declarations across the workspace, from its index and the open
    // documents.
    capabilities.set("workspaceSymbolProvider", json::Value(true));
    // Every place references finds, rewritten; a client that can ask
    // first learns what would be.
    if (server_.client_capabilities().prepare_rename) {
        json::Value rename = json::Value::object();
        rename.set("prepareProvider", json::Value(true));
        capabilities.set("renameProvider", std::move(rename));
    } else {
        capabilities.set("renameProvider", json::Value(true));
    }
    // Folding and expanding a selection by the structure Clang parsed and
    // the C++L structure the recognizer found.
    capabilities.set("foldingRangeProvider", json::Value(true));
    capabilities.set("selectionRangeProvider", json::Value(true));
    // Parameter names at arguments and the types `auto` deduced, from Clang.
    capabilities.set("inlayHintProvider", json::Value(true));

    // What became of each Law's, proof's and verified function's
    // obligations, stated over its name; a lens runs no command.
    json::Value code_lens = json::Value::object();
    code_lens.set("resolveProvider", json::Value(false));
    capabilities.set("codeLensProvider", std::move(code_lens));

    json::Value server_info = json::Value::object();
    server_info.set("name", json::Value("cppl-lsp"));

    json::Value result = json::Value::object();
    result.set("capabilities", capabilities);
    result.set("serverInfo", server_info);

    if (id != nullptr) {
        respond_result(*id, result);
    }
}

void Dispatcher::handle_did_open(const json::Value* params) {
    if (params == nullptr) {
        log_ << "cppl-lsp: textDocument/didOpen with no params\n";
        return;
    }
    const json::Value* document = params->find("textDocument");
    if (document == nullptr) {
        log_ << "cppl-lsp: textDocument/didOpen missing 'textDocument'\n";
        return;
    }
    const auto uri = document->find_string("uri");
    const auto text = document->find_string("text");
    const auto language_id = document->find_string("languageId");
    const auto version = document->find_number("version");
    if (!uri || !text) {
        log_ << "cppl-lsp: textDocument/didOpen missing 'uri' or 'text'\n";
        return;
    }
    TextDocumentItem item;
    item.uri = *uri;
    item.text = *text;
    item.languageId = language_id.value_or("cppl");
    item.version = checked_version(version, "textDocument/didOpen");
    server_.text_document_did_open(item);
}

std::int32_t Dispatcher::checked_version(std::optional<double> version, std::string_view method) {
    if (!version) {
        return 0;
    }
    const std::optional<std::int32_t> checked = parse_version(*version);
    if (!checked) {
        log_ << "cppl-lsp: " << method << " has an out-of-range 'version'; recording 0\n";
    }
    return checked.value_or(0);
}

void Dispatcher::handle_did_change(const json::Value* params) {
    if (params == nullptr) {
        log_ << "cppl-lsp: textDocument/didChange with no params\n";
        return;
    }
    const json::Value* document = params->find("textDocument");
    const json::Value* changes = params->find("contentChanges");
    if (document == nullptr || changes == nullptr || !changes->is_array()) {
        log_ << "cppl-lsp: textDocument/didChange missing 'textDocument' or 'contentChanges'\n";
        return;
    }
    const auto uri = document->find_string("uri");
    const auto version = document->find_number("version");
    if (!uri) {
        log_ << "cppl-lsp: textDocument/didChange missing 'uri'\n";
        return;
    }
    VersionedTextDocumentIdentifier id;
    id.uri = *uri;
    id.version = checked_version(version, "textDocument/didChange");

    std::vector<TextDocumentContentChangeEvent> events;
    for (const json::Value& change : changes->as_array()) {
        const auto text = change.find_string("text");
        if (!text) {
            continue; // a malformed change entry: skip rather than abort the batch
        }
        TextDocumentContentChangeEvent event;
        event.text = *text;
        if (const json::Value* range = change.find("range")) {
            event.range = parse_range(*range);
            if (!event.range) {
                // Taken as a whole-document change, the fragment would
                // silently become the entire buffer.
                log_ << "cppl-lsp: textDocument/didChange skipping a change with a malformed 'range'\n";
                continue;
            }
        }
        events.push_back(std::move(event));
    }
    if (!events.empty()) {
        server_.text_document_did_change(id, events);
    }
}

void Dispatcher::handle_did_close(const json::Value* params) {
    if (params == nullptr) {
        log_ << "cppl-lsp: textDocument/didClose with no params\n";
        return;
    }
    const json::Value* document = params->find("textDocument");
    const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
    if (!uri) {
        log_ << "cppl-lsp: textDocument/didClose missing 'uri'\n";
        return;
    }
    TextDocumentIdentifier id;
    id.uri = *uri;
    server_.text_document_did_close(id);
}

void Dispatcher::publish_diagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics) {
    json::Value params = json::Value::object();
    params.set("uri", json::Value(uri));
    json::Value items = json::Value::array();
    for (const Diagnostic& diagnostic : diagnostics) {
        items.push_back(diagnostic_to_json(diagnostic));
    }
    params.set("diagnostics", items);

    json::Value message = json::Value::object();
    message.set("jsonrpc", json::Value("2.0"));
    message.set("method", json::Value("textDocument/publishDiagnostics"));
    message.set("params", params);
    write(message);
}

} // namespace cppl::lsp
