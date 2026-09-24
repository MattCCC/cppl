#include "cppl/lsp/transport.hpp"

#include "cppl/clang/editor.hpp"
#include "cppl/lsp/json.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/semantic_tokens.hpp"
#include "cppl/lsp/server.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <ios>
#include <istream>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cppl::lsp {

namespace {

// The editor on the other end of the pipe states each message's length before
// sending it, so these bound what a stated length can cost. A header line is a
// name and a number. A message body is at most one document's text; one this
// large is not a C++L source anyone edits.
constexpr std::size_t kKiB = 1024;
constexpr std::size_t kMaxHeaderLine = 8 * kKiB;
constexpr std::size_t kMaxBody = 64 * kKiB * kKiB;
constexpr std::size_t kReadChunk = 64 * kKiB;

// One header line without its terminator, or nullopt at end of stream. LSP
// ends each line with "\r\n"; a bare "\n" is accepted too.
[[nodiscard]] std::optional<std::string> read_header_line(std::istream& input) {
    std::string line;
    char c = '\0';
    while (input.get(c)) {
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }
        if (line.size() == kMaxHeaderLine) {
            throw json::Error("message header line longer than 8 KiB");
        }
        line.push_back(c);
    }
    return std::nullopt;
}

[[nodiscard]] bool is_blank(char c) {
    return c == ' ' || c == '\t';
}

// Decimal digits only, around optional blanks. std::stoul would also take a
// sign (so "-1" read as the largest length), leading blanks of every kind and
// any trailing text.
[[nodiscard]] std::size_t parse_content_length(std::string_view value) {
    while (!value.empty() && is_blank(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && is_blank(value.back())) {
        value.remove_suffix(1);
    }
    std::size_t length = 0;
    const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), length);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw json::Error("malformed Content-Length header");
    }
    if (length > kMaxBody) {
        throw json::Error("Content-Length larger than 64 MiB");
    }
    return length;
}

// Reads the Content-Length header block: one or more "Name: Value" lines
// ending in a blank line. Only Content-Length is required by the LSP base
// protocol; any other header (e.g. Content-Type) is accepted and ignored, as
// is a line with no colon. A second Content-Length is refused: believing
// either one would read the stream differently from a peer that believed the
// other.
[[nodiscard]] std::optional<std::size_t> read_headers(std::istream& input) {
    std::optional<std::size_t> content_length;
    while (const std::optional<std::string> line = read_header_line(input)) {
        if (line->empty()) {
            return content_length;
        }
        const std::size_t colon = line->find(':');
        if (colon == std::string::npos) {
            continue;
        }
        if (std::string_view(*line).substr(0, colon) != "Content-Length") {
            continue;
        }
        if (content_length.has_value()) {
            throw json::Error("more than one Content-Length header");
        }
        content_length = parse_content_length(std::string_view(*line).substr(colon + 1));
    }
    return std::nullopt; // end of stream before a header block completed
}

json::Value make_error(int code, const std::string& message) {
    json::Value error = json::Value::object();
    error.set("code", json::Value(code));
    error.set("message", json::Value(message));
    return error;
}

constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
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

// Dispatches one JSON-RPC message to `server`. Writes a response to
// `output` for a request (a message carrying "id"); a notification (no
// "id") produces no response either way, per the JSON-RPC 2.0 spec.
class Dispatcher {
  public:
    Dispatcher(Server& server, std::ostream& output, std::ostream& log) : server_(server), output_(output), log_(log) {
        server_.set_diagnostic_publisher([this](const std::string& uri, const std::vector<Diagnostic>& diagnostics) {
            publish_diagnostics(uri, diagnostics);
        });
    }

    // Returns false once `exit` has been processed, telling the caller to
    // stop reading further messages.
    bool dispatch(const json::Value& message) {
        const json::Value* method_value = message.find("method");
        const json::Value* id_value = message.find("id");
        const bool is_request = id_value != nullptr;

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

    [[nodiscard]] bool exited() const noexcept {
        return exited_;
    }

  private:
    Server& server_;
    std::ostream& output_;
    std::ostream& log_;
    bool shutting_down_ = false;
    bool exited_ = false;

    void write(const json::Value& message) {
        write_message(output_, message.dump());
    }

    void respond_result(const json::Value& id, json::Value result) {
        json::Value message = json::Value::object();
        message.set("jsonrpc", json::Value("2.0"));
        message.set("id", id);
        message.set("result", std::move(result));
        write(message);
    }

    void respond_error(const json::Value& id, int code, const std::string& text) {
        json::Value message = json::Value::object();
        message.set("jsonrpc", json::Value("2.0"));
        message.set("id", id);
        message.set("error", make_error(code, text));
        write(message);
    }

    // What the client said it can do, where it changes what is sent.
    static ClientCapabilities client_capabilities(const json::Value* params) {
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
        if (const json::Value* nested =
                symbols != nullptr ? symbols->find("hierarchicalDocumentSymbolSupport") : nullptr;
            nested != nullptr && nested->is_boolean()) {
            capabilities.hierarchical_symbols = nested->as_boolean();
        }
        const json::Value* folding = document != nullptr ? document->find("foldingRange") : nullptr;
        if (const json::Value* lines = folding != nullptr ? folding->find("lineFoldingOnly") : nullptr;
            lines != nullptr && lines->is_boolean()) {
            capabilities.line_folding_only = lines->as_boolean();
        }
        return capabilities;
    }

    void handle_initialize(const json::Value* id, const json::Value* params) {
        server_.initialize(client_capabilities(params));

        json::Value capabilities = json::Value::object();
        // Full document sync: Document::apply_change currently replaces the
        // whole text on any change, so advertising Incremental would be a
        // capability the server does not actually implement
        // (tools/cppl-lsp/README.md non-goals: no incremental sync yet).
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

        // The proof-statement keywords the TextMate grammar cannot tell from a
        // C++ declaration. Whole-document only: the set is small, and a range
        // or delta request would buy nothing for it.
        json::Value token_types = json::Value::array();
        token_types.push_back(json::Value(std::string(kKeywordTokenType)));
        json::Value legend = json::Value::object();
        legend.set("tokenTypes", std::move(token_types));
        legend.set("tokenModifiers", json::Value::array());
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
        // Folding and expanding a selection by the structure Clang parsed and
        // the C++L structure the recognizer found.
        capabilities.set("foldingRangeProvider", json::Value(true));
        capabilities.set("selectionRangeProvider", json::Value(true));

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

    void handle_did_open(const json::Value* params) {
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

    // The version a notification carries, or 0 when it carries none or one no
    // `std::int32_t` holds. A version is only recorded, so a bad one is logged
    // rather than allowed to cost the text it arrived with.
    std::int32_t checked_version(std::optional<double> version, std::string_view method) {
        if (!version) {
            return 0;
        }
        const std::optional<std::int32_t> checked = parse_version(*version);
        if (!checked) {
            log_ << "cppl-lsp: " << method << " has an out-of-range 'version'; recording 0\n";
        }
        return checked.value_or(0);
    }

    void handle_did_change(const json::Value* params) {
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

    void handle_did_close(const json::Value* params) {
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

    static json::Value text_edits_to_json(const std::vector<TextEdit>& edits) {
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

    // `std::nullopt` (unknown document) responds with LSP's own `null`
    // result, per the base protocol; a known-but-already-canonical document
    // responds with an empty array, which is a different, meaningful result.
    void respond_edits(const json::Value& id, std::optional<std::vector<TextEdit>> edits) {
        if (!edits.has_value()) {
            respond_result(id, json::Value(nullptr));
            return;
        }
        respond_result(id, text_edits_to_json(*edits));
    }

    void handle_formatting(const json::Value* id, const json::Value* params) {
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

    // An empty completion list and a null hover are ordinary answers: nothing
    // is offered there, or nothing is named there. Neither is an error.
    void handle_completion(const json::Value* id, const json::Value* params) {
        if (id == nullptr) {
            return;
        }
        const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
        const json::Value* position_value = params != nullptr ? params->find("position") : nullptr;
        const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
        const std::optional<Position> position =
            position_value != nullptr ? parse_position(*position_value) : std::nullopt;
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

    void handle_hover(const json::Value* id, const json::Value* params) {
        if (id == nullptr) {
            return;
        }
        const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
        const json::Value* position_value = params != nullptr ? params->find("position") : nullptr;
        const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
        const std::optional<Position> position =
            position_value != nullptr ? parse_position(*position_value) : std::nullopt;
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

    // The document and position a `TextDocumentPositionParams` names.
    static std::optional<std::pair<TextDocumentIdentifier, Position>> position_params(const json::Value* params) {
        const json::Value* document = params != nullptr ? params->find("textDocument") : nullptr;
        const json::Value* position_value = params != nullptr ? params->find("position") : nullptr;
        const auto uri = document != nullptr ? document->find_string("uri") : std::nullopt;
        const std::optional<Position> position =
            position_value != nullptr ? parse_position(*position_value) : std::nullopt;
        if (!uri || !position.has_value()) {
            return std::nullopt;
        }
        TextDocumentIdentifier document_id;
        document_id.uri = *uri;
        return std::pair{document_id, *position};
    }

    static json::Value range_to_json(const Range& range) {
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

    static json::Value location_to_json(const Location& location) {
        json::Value result = json::Value::object();
        result.set("uri", json::Value(location.uri));
        result.set("range", range_to_json(location.range));
        return result;
    }

    // An empty list is an ordinary answer: nothing is named there, or nothing
    // Clang found can be traced to text an author wrote.
    void handle_navigate(const json::Value* id, const json::Value* params, clangbridge::Destination destination,
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

    void handle_references(const json::Value* id, const json::Value* params) {
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

    void handle_document_highlight(const json::Value* id, const json::Value* params) {
        if (id == nullptr) {
            return;
        }
        const auto request = position_params(params);
        if (!request.has_value()) {
            respond_error(*id, kInvalidParams,
                          "textDocument/documentHighlight missing 'textDocument.uri' or 'position'");
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

    void handle_signature_help(const json::Value* id, const json::Value* params) {
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

    static json::Value document_symbol_to_json(const DocumentSymbol& symbol) {
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

    // A client that cannot nest an outline is sent every entry in order, each
    // naming the entry it is nested in (LSP `SymbolInformation`).
    static void flatten_symbols(const std::vector<DocumentSymbol>& symbols, const std::string& uri,
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
            flatten_symbols(symbol.children, uri, container.empty() ? symbol.name : container + "::" + symbol.name,
                            into);
        }
    }

    void handle_document_symbol(const json::Value* id, const json::Value* params) {
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

    void handle_folding_range(const json::Value* id, const json::Value* params) {
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

    // A client asks for a selection at each cursor it has; more than any
    // editor keeps is refused rather than parsed and walked one by one.
    static constexpr std::size_t kMaxSelectionPositions = 4096;

    void handle_selection_range(const json::Value* id, const json::Value* params) {
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

    void handle_code_lens(const json::Value* id, const json::Value* params) {
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

    void handle_semantic_tokens(const json::Value* id, const json::Value* params) {
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

    void handle_code_action(const json::Value* id, const json::Value* params) {
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

    void handle_range_formatting(const json::Value* id, const json::Value* params) {
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

    void handle_on_type_formatting(const json::Value* id, const json::Value* params) {
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

    void publish_diagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics) {
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
};

} // namespace

std::optional<std::string> read_message(std::istream& input) {
    const std::optional<std::size_t> length = read_headers(input);
    if (!length.has_value()) {
        return std::nullopt;
    }
    // The body grows as its bytes arrive, so a length that is stated and never
    // sent costs nothing.
    std::string body;
    std::string chunk;
    while (body.size() < *length) {
        chunk.resize(std::min(kReadChunk, *length - body.size()));
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto received = static_cast<std::size_t>(input.gcount());
        body.append(chunk, 0, received);
        if (received != chunk.size()) {
            return std::nullopt; // truncated body: the stream ended mid-message
        }
    }
    return body;
}

void write_message(std::ostream& output, const std::string& body) {
    output << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    output.flush();
}

int run_transport(Server& server, std::istream& input, std::ostream& output, std::ostream& log) {
    Dispatcher dispatcher(server, output, log);

    while (true) {
        std::optional<std::string> body;
        try {
            body = read_message(input);
        } catch (const std::exception& error) {
            log << "cppl-lsp: malformed message header: " << error.what() << "\n";
            break;
        }
        if (!body.has_value()) {
            break; // end of stream, or a header block that could not be read
        }

        json::Value message;
        try {
            message = json::parse(*body);
        } catch (const std::exception& error) {
            log << "cppl-lsp: malformed JSON-RPC body: " << error.what() << "\n";
            continue; // one bad message does not end the session
        }

        bool keep_going = true;
        try {
            keep_going = dispatcher.dispatch(message);
        } catch (const std::exception& error) {
            log << "cppl-lsp: error handling a request: " << error.what() << "\n";
            // A handler failure is not fatal to the session: continue.
        }
        if (!keep_going) {
            break;
        }
    }

    return server.is_shutting_down() ? 0 : 1;
}

} // namespace cppl::lsp
