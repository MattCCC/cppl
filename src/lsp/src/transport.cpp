#include "cppl/lsp/transport.hpp"

#include <istream>
#include <ostream>
#include <sstream>

namespace cppl::lsp {

namespace {

// Reads the Content-Length header block: one or more "Name: Value\r\n"
// lines terminated by a blank line. Only Content-Length is required by the
// LSP base protocol; any other header (e.g. Content-Type) is accepted and
// ignored.
[[nodiscard]] std::optional<std::size_t> read_headers(std::istream& input) {
    std::optional<std::size_t> content_length;
    std::string line;
    while (std::getline(input, line)) {
        // getline stops at '\n'; LSP headers end each line with "\r\n", so a
        // trailing '\r' is stripped here.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            // The blank line ending the header block.
            return content_length;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue; // malformed header line: ignore rather than abort the session
        }
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }
        if (name == "Content-Length") {
            try {
                content_length = static_cast<std::size_t>(std::stoul(value));
            } catch (const std::exception&) {
                return std::nullopt; // malformed length: nothing sound can be read
            }
        }
    }
    return std::nullopt; // end of stream before a header block completed
}

json::Value make_error(int code, const std::string& message) {
    json::Value error = json::Value::object();
    error.set("code", json::Value(code));
    error.set("message", json::Value(message));
    return error;
}

constexpr int kParseError = -32700;
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

std::optional<Range> parse_range(const json::Value& value) {
    const json::Value* start = value.find("start");
    const json::Value* end = value.find("end");
    if (start == nullptr || end == nullptr) {
        return std::nullopt;
    }
    const auto start_line = start->find_number("line");
    const auto start_character = start->find_number("character");
    const auto end_line = end->find_number("line");
    const auto end_character = end->find_number("character");
    if (!start_line || !start_character || !end_line || !end_character) {
        return std::nullopt;
    }
    Range range;
    range.start.line = static_cast<std::uint32_t>(*start_line);
    range.start.character = static_cast<std::uint32_t>(*start_character);
    range.end.line = static_cast<std::uint32_t>(*end_line);
    range.end.character = static_cast<std::uint32_t>(*end_character);
    return range;
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

    void handle_initialize(const json::Value* id, const json::Value*) {
        server_.initialize();

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
        item.version = version ? static_cast<std::int32_t>(*version) : 0;
        server_.text_document_did_open(item);
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
        id.version = version ? static_cast<std::int32_t>(*version) : 0;

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
        const auto line = position_value->find_number("line");
        const auto column = position_value->find_number("character");
        if (!line || !column) {
            respond_error(*id, kInvalidParams, "textDocument/onTypeFormatting has a malformed 'position'");
            return;
        }
        Position position;
        position.line = static_cast<std::uint32_t>(*line);
        position.character = static_cast<std::uint32_t>(*column);

        TextDocumentIdentifier document_id;
        document_id.uri = *uri;
        respond_edits(*id, server_.text_document_on_type_formatting(document_id, position, *character));
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
    std::string body(*length, '\0');
    input.read(body.data(), static_cast<std::streamsize>(*length));
    if (static_cast<std::size_t>(input.gcount()) != *length) {
        return std::nullopt; // truncated body: the stream ended mid-message
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
