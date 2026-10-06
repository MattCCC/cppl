#pragma once

// The dispatcher that answers each JSON-RPC message, and what it shares with
// the transport: the protocol channel and the progress tokens the client
// created. Its handlers are defined in transport_*.cpp by what they answer.

#include "cppl/clang/editor.hpp"
#include "cppl/lsp/json.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/lsp/transport.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::lsp::detail::transport {

inline constexpr int kInvalidParams = -32602;

// Defined in transport_dispatch.cpp.
std::optional<Position> parse_position(const json::Value& value);

std::optional<Range> parse_range(const json::Value& value);

// A request the server understood and declined, saying why (LSP
// `RequestFailed`), as a rename it will not make.
inline constexpr int kRequestFailed = -32803;

// The protocol channel. Each message is written whole, from whichever thread
// has one to send.
class Writer {
  public:
    explicit Writer(std::ostream& output) : output_(output) {}

    void write(const json::Value& message) {
        const std::string body = message.dump();
        const std::scoped_lock lock(mutex_);
        write_message(output_, body);
    }

  private:
    std::ostream& output_;
    std::mutex mutex_;
};

// Defined in transport_dispatch.cpp.
json::Value request(const std::string& id, const std::string& method, json::Value params);

// Tokens the client created for progress this server reports
// (`window/workDoneProgress/create`). A token carries one piece of work from
// its beginning to its end, so each is taken once.
class ProgressTokens {
  public:
    std::optional<std::string> take() {
        const std::scoped_lock lock(mutex_);
        if (ready_.empty()) {
            return std::nullopt;
        }
        std::string token = std::move(ready_.front());
        ready_.pop_front();
        return token;
    }

    void add(std::string token) {
        const std::scoped_lock lock(mutex_);
        ready_.push_back(std::move(token));
    }

  private:
    std::mutex mutex_;
    std::deque<std::string> ready_;
};

// Dispatches one JSON-RPC message to `server`. Writes a response to
// `output` for a request (a message carrying "id"); a notification (no
// "id") produces no response either way, per the JSON-RPC 2.0 spec.
class Dispatcher {
  public:
    Dispatcher(Server& server, Writer& writer, std::ostream& log, ProgressTokens* tokens = nullptr)
        : server_(server),
          writer_(writer),
          log_(log),
          tokens_(tokens) {
        server_.set_diagnostic_publisher([this](const std::string& uri, const std::vector<Diagnostic>& diagnostics) {
            publish_diagnostics(uri, diagnostics);
        });
    }

    // Asks the client for a progress token to report the next compile, or the
    // next pass of indexing, with.
    void request_progress_token();

    // Tells the client that what a compile decides -- each verdict's lens,
    // which statements are claims -- may have changed.
    void refresh_after_compile();

    // Answers a request the client withdrew before it was dispatched.
    void cancelled(const json::Value& id);

    // Returns false once `exit` has been processed, telling the caller to
    // stop reading further messages.
    bool dispatch(const json::Value& message);

    [[nodiscard]] bool exited() const noexcept;

  private:
    Server& server_;
    Writer& writer_;
    std::ostream& log_;
    ProgressTokens* tokens_ = nullptr;
    bool shutting_down_ = false;
    bool exited_ = false;
    std::uint64_t sent_ = 0;
    // Progress tokens asked for and not yet created, by the id of the request.
    std::map<std::string, std::string> pending_tokens_;

    void write(const json::Value& message);

    void answered(const json::Value& id, bool succeeded);

    void respond_result(const json::Value& id, json::Value result);

    void respond_error(const json::Value& id, int code, const std::string& text);

    // What the client said it can do, where it changes what is sent.
    static ClientCapabilities client_capabilities(const json::Value* params);

    // The directories the client opened: each workspace folder, or else the
    // root it names, as a URI or, from an older client, a path.
    static std::vector<std::string> workspace_roots(const json::Value* params);

    void handle_initialize(const json::Value* id, const json::Value* params);

    void handle_did_open(const json::Value* params);

    // The version a notification carries, or 0 when it carries none or one no
    // `std::int32_t` holds. A version is only recorded, so a bad one is logged
    // rather than allowed to cost the text it arrived with.
    std::int32_t checked_version(std::optional<double> version, std::string_view method);

    void handle_did_change(const json::Value* params);

    void handle_did_close(const json::Value* params);

    static json::Value text_edits_to_json(const std::vector<TextEdit>& edits);

    // `std::nullopt` (unknown document) responds with LSP's own `null`
    // result, per the base protocol; a known-but-already-canonical document
    // responds with an empty array, which is a different, meaningful result.
    void respond_edits(const json::Value& id, std::optional<std::vector<TextEdit>> edits);

    void handle_formatting(const json::Value* id, const json::Value* params);

    // An empty completion list and a null hover are ordinary answers: nothing
    // is offered there, or nothing is named there. Neither is an error.
    void handle_completion(const json::Value* id, const json::Value* params);

    void handle_hover(const json::Value* id, const json::Value* params);

    // The document and position a `TextDocumentPositionParams` names.
    static std::optional<std::pair<TextDocumentIdentifier, Position>> position_params(const json::Value* params);

    static json::Value range_to_json(const Range& range);

    static json::Value location_to_json(const Location& location);

    // An empty list is an ordinary answer: nothing is named there, or nothing
    // Clang found can be traced to text an author wrote.
    void handle_navigate(const json::Value* id, const json::Value* params, clangbridge::Destination destination,
                         const std::string& method);

    void handle_references(const json::Value* id, const json::Value* params);

    void handle_document_highlight(const json::Value* id, const json::Value* params);

    void handle_signature_help(const json::Value* id, const json::Value* params);

    static json::Value document_symbol_to_json(const DocumentSymbol& symbol);

    // A client that cannot nest an outline is sent every entry in order, each
    // naming the entry it is nested in (LSP `SymbolInformation`).
    static void flatten_symbols(const std::vector<DocumentSymbol>& symbols, const std::string& uri,
                                const std::string& container, json::Value& into);

    void handle_document_symbol(const json::Value* id, const json::Value* params);

    void handle_workspace_symbol(const json::Value* id, const json::Value* params);

    void handle_prepare_rename(const json::Value* id, const json::Value* params);

    void handle_rename(const json::Value* id, const json::Value* params);

    void handle_folding_range(const json::Value* id, const json::Value* params);

    void handle_inlay_hint(const json::Value* id, const json::Value* params);

    // A client asks for a selection at each cursor it has; more than any
    // editor keeps is refused rather than parsed and walked one by one.
    static constexpr std::size_t kMaxSelectionPositions = 4096;

    void handle_selection_range(const json::Value* id, const json::Value* params);

    void handle_code_lens(const json::Value* id, const json::Value* params);

    void handle_semantic_tokens(const json::Value* id, const json::Value* params);

    void handle_code_action(const json::Value* id, const json::Value* params);

    void handle_range_formatting(const json::Value* id, const json::Value* params);

    void handle_on_type_formatting(const json::Value* id, const json::Value* params);

    void publish_diagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics);
};

} // namespace cppl::lsp::detail::transport
