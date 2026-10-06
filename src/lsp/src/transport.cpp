#include "cppl/lsp/transport.hpp"

#include "cppl/lsp/json.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/lsp/workspace_index.hpp"
#include "transport_dispatcher.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <filesystem>
#include <future>
#include <ios>
#include <istream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>

namespace cppl::lsp {

using detail::transport::Dispatcher;
using detail::transport::ProgressTokens;
using detail::transport::Writer;

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

// Requests the client withdrew (`$/cancelRequest`) while they waited to be
// answered, by their id as the client spelled it. A withdrawal that arrives
// after its request was answered is kept only until more recent ones push it
// out, so the set stays small.
class Cancellations {
  public:
    void add(std::string id) {
        const std::scoped_lock lock(mutex_);
        if (ids_.insert(id).second) {
            order_.push_back(std::move(id));
        }
        if (order_.size() > kKept) {
            ids_.erase(order_.front());
            order_.pop_front();
        }
    }

    // Whether the request with `id` was withdrawn, forgetting it if so.
    bool take(const std::string& id) {
        const std::scoped_lock lock(mutex_);
        return ids_.erase(id) != 0;
    }

  private:
    static constexpr std::size_t kKept = 4096;
    std::mutex mutex_;
    std::set<std::string> ids_;
    std::deque<std::string> order_;
};

// What the loop that owns the server is handed, in the order it happened: a
// message the client sent, a line to log, the end of the input, a compile
// that finished, or a progress token taken, to be replaced.
struct Incoming {
    json::Value message;
};
struct Logged {
    std::string line;
};
struct InputEnded {};
struct Compiled {
    CompileResult result;
};
struct TokenTaken {};
using Event = std::variant<Incoming, Logged, InputEnded, Compiled, TokenTaken>;

class Events {
  public:
    void push(Event event) {
        {
            const std::scoped_lock lock(mutex_);
            events_.push_back(std::move(event));
        }
        ready_.notify_one();
    }

    Event pop() {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [this] { return !events_.empty(); });
        Event event = std::move(events_.front());
        events_.pop_front();
        return event;
    }

  private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Event> events_;
};

void report_progress(Writer& writer, const std::string& token, json::Value value) {
    json::Value params = json::Value::object();
    params.set("token", json::Value(token));
    params.set("value", std::move(value));
    json::Value message = json::Value::object();
    message.set("jsonrpc", json::Value("2.0"));
    message.set("method", json::Value("$/progress"));
    message.set("params", std::move(params));
    writer.write(message);
}

// Reports indexing, on the thread that indexes: each pass that reads files
// begins on a token, reports each file read, and ends once all are. A pass
// that begins before a token has been created takes one as it arrives.
class IndexProgress {
  public:
    IndexProgress(Writer& writer, ProgressTokens& tokens, Events& events)
        : writer_(writer),
          tokens_(tokens),
          events_(events) {}

    void operator()(std::size_t done, std::size_t total) {
        if (done == 0 && token_.has_value()) {
            end();
        }
        if (done >= total) {
            end();
            return;
        }
        const std::string counted = std::to_string(done) + "/" + std::to_string(total) + " files";
        constexpr std::size_t kWhole = 100;
        const auto percentage = static_cast<int>(done * kWhole / total);
        if (!token_.has_value()) {
            token_ = tokens_.take();
            if (!token_.has_value()) {
                return;
            }
            json::Value begin = json::Value::object();
            begin.set("kind", json::Value("begin"));
            begin.set("title", json::Value("Indexing"));
            begin.set("message", json::Value(counted));
            begin.set("percentage", json::Value(percentage));
            begin.set("cancellable", json::Value(false));
            report_progress(writer_, *token_, std::move(begin));
            events_.push(TokenTaken{});
            return;
        }
        json::Value report = json::Value::object();
        report.set("kind", json::Value("report"));
        report.set("message", json::Value(counted));
        report.set("percentage", json::Value(percentage));
        report_progress(writer_, *token_, std::move(report));
    }

  private:
    void end() {
        if (!token_.has_value()) {
            return;
        }
        json::Value end = json::Value::object();
        end.set("kind", json::Value("end"));
        report_progress(writer_, *token_, std::move(end));
        token_.reset();
    }

    Writer& writer_;
    ProgressTokens& tokens_;
    Events& events_;
    std::optional<std::string> token_;
};

// Compiles documents on a thread of its own, so the loop that owns the server
// answers requests while a compile runs. Each document keeps only its latest
// text waiting: a change waits for typing to pause before it is compiled,
// and a later change replaces it. What a compile produced is handed back as
// an event, to be applied where the server lives.
class CompileWorker {
  public:
    using Clock = std::chrono::steady_clock;

    CompileWorker(Events& events, Writer& writer, ProgressTokens& tokens)
        : events_(events),
          writer_(writer),
          tokens_(tokens),
          thread_([this] { run(); }) {}

    ~CompileWorker() {
        stop();
    }

    CompileWorker(const CompileWorker&) = delete;
    CompileWorker& operator=(const CompileWorker&) = delete;
    CompileWorker(CompileWorker&&) = delete;
    CompileWorker& operator=(CompileWorker&&) = delete;

    void schedule(CompileJob job, Clock::duration delay) {
        {
            const std::scoped_lock lock(mutex_);
            std::string uri = job.uri;
            waiting_.insert_or_assign(std::move(uri), Waiting{std::move(job), Clock::now() + delay});
        }
        changed_.notify_one();
    }

    // Drops what waits, and returns once the compile running, if any, ends.
    void stop() {
        {
            const std::scoped_lock lock(mutex_);
            stopping_ = true;
            waiting_.clear();
        }
        changed_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

  private:
    struct Waiting {
        CompileJob job;
        Clock::time_point due;
    };

    void run() {
        std::unique_lock lock(mutex_);
        while (!stopping_) {
            if (waiting_.empty()) {
                changed_.wait(lock);
                continue;
            }
            const auto next = std::ranges::min_element(
                waiting_, {}, [](const std::pair<const std::string, Waiting>& entry) { return entry.second.due; });
            // A copy: while this waits, a later change may replace the entry.
            if (const Clock::time_point due = next->second.due; due > Clock::now()) {
                changed_.wait_until(lock, due);
                continue;
            }
            CompileJob job = std::move(next->second.job);
            waiting_.erase(next);
            lock.unlock();
            compile_and_hand_back(job);
            lock.lock();
        }
    }

    void compile_and_hand_back(const CompileJob& job) {
        const std::optional<std::string> token = tokens_.take();
        if (token.has_value()) {
            json::Value begin = json::Value::object();
            begin.set("kind", json::Value("begin"));
            begin.set("title", json::Value("Checking"));
            begin.set("message", json::Value(std::filesystem::path(job.request.virtual_path).filename().string()));
            begin.set("cancellable", json::Value(false));
            report_progress(writer_, *token, std::move(begin));
            events_.push(TokenTaken{});
        }
        try {
            events_.push(Compiled{compile(job)});
        } catch (const std::exception& error) {
            events_.push(Logged{std::string("cppl-lsp: compiling '") + job.uri + "' failed: " + error.what()});
        }
        if (token.has_value()) {
            json::Value end = json::Value::object();
            end.set("kind", json::Value("end"));
            report_progress(writer_, *token, std::move(end));
        }
    }

    Events& events_;
    Writer& writer_;
    ProgressTokens& tokens_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::map<std::string, Waiting> waiting_;
    bool stopping_ = false;
    // Last, so that it starts once everything it uses exists.
    std::thread thread_;
};

// What the reader thread and the loop share. The reader may outlive the loop
// (a client that keeps its end of the input open after `exit` leaves the
// reader blocked), so it holds its share alive itself.
struct Shared {
    Events events;
    Cancellations cancelled;
};

// Reads messages until the input ends, handing each to the loop in order.
// A withdrawal (`$/cancelRequest`) is recorded at once instead, so that it can
// overtake the request it withdraws, which may still be waiting its turn.
void read_messages(std::istream& input, const std::shared_ptr<Shared>& shared) {
    while (true) {
        std::optional<std::string> body;
        try {
            body = read_message(input);
        } catch (const std::exception& error) {
            shared->events.push(Logged{std::string("cppl-lsp: malformed message header: ") + error.what()});
            break;
        }
        if (!body.has_value()) {
            break; // end of stream, or a header block that could not be read
        }
        json::Value message;
        try {
            message = json::parse(*body);
        } catch (const std::exception& error) {
            shared->events.push(Logged{std::string("cppl-lsp: malformed JSON-RPC body: ") + error.what()});
            continue; // one bad message does not end the session
        }
        if (const std::optional<std::string> method = message.find_string("method"); method == "$/cancelRequest") {
            const json::Value* params = message.find("params");
            if (const json::Value* id = params != nullptr ? params->find("id") : nullptr) {
                shared->cancelled.add(id->dump());
            }
            continue;
        }
        shared->events.push(Incoming{std::move(message)});
    }
    shared->events.push(InputEnded{});
}

bool dispatch_logged(Dispatcher& dispatcher, const json::Value& message, std::ostream& log) {
    try {
        return dispatcher.dispatch(message);
    } catch (const std::exception& error) {
        log << "cppl-lsp: error handling a request: " << error.what() << "\n";
        return true; // a handler failure is not fatal to the session
    }
}

// The loop with compiles in the background: every message, finished compile
// and line to log arrives as an event, handled in order where the server
// lives.
void run_in_background(Server& server, std::istream& input, std::ostream& output, std::ostream& log,
                       const TransportOptions& options) {
    Writer writer(output);
    ProgressTokens tokens;
    Dispatcher dispatcher(server, writer, log, &tokens);
    const auto shared = std::make_shared<Shared>();
    CompileWorker worker(shared->events, writer, tokens);
    server.set_compile_scheduler([&worker, quiet = options.quiet](CompileJob job, bool opened) {
        worker.schedule(std::move(job), opened ? CompileWorker::Clock::duration::zero() : quiet);
    });
    IndexProgress index_progress(writer, tokens, shared->events);
    server.set_index_progress([&index_progress](std::size_t done, std::size_t total) { index_progress(done, total); });

    std::promise<void> read_all;
    std::future<void> reader_done = read_all.get_future();
    std::thread reader([&input, shared, done = std::move(read_all)]() mutable {
        read_messages(input, shared);
        done.set_value();
    });

    bool running = true;
    while (running) {
        Event event = shared->events.pop();
        if (auto* incoming = std::get_if<Incoming>(&event)) {
            const json::Value* id = incoming->message.find("id");
            if (id != nullptr && incoming->message.find("method") != nullptr && shared->cancelled.take(id->dump())) {
                dispatcher.cancelled(*id);
                continue;
            }
            running = dispatch_logged(dispatcher, incoming->message, log);
        } else if (auto* logged = std::get_if<Logged>(&event)) {
            log << logged->line << "\n";
        } else if (auto* compiled = std::get_if<Compiled>(&event)) {
            server.apply_compile(std::move(compiled->result));
            dispatcher.refresh_after_compile();
        } else if (std::holds_alternative<TokenTaken>(event)) {
            dispatcher.request_progress_token();
        } else {
            running = false; // the input ended
        }
    }

    // Indexing reports through what ends with this loop, so it ends first.
    server.stop_index();
    server.set_index_progress({});
    server.set_compile_scheduler({});
    worker.stop();
    // A client closes its end of the input after `exit`, which ends the reader;
    // one that keeps it open leaves the reader blocked, and it is let go
    // rather than waited for, holding what it shares alive.
    constexpr auto kReaderGrace = std::chrono::seconds(2);
    if (reader_done.wait_for(kReaderGrace) == std::future_status::ready) {
        reader.join();
    } else {
        reader.detach();
    }
}

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

int run_transport(Server& server, std::istream& input, std::ostream& output, std::ostream& log,
                  const TransportOptions& options) {
    if (options.background_compiles) {
        run_in_background(server, input, output, log, options);
        return server.is_shutting_down() ? 0 : 1;
    }

    Writer writer(output);
    Dispatcher dispatcher(server, writer, log);
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

        if (!dispatch_logged(dispatcher, message, log)) {
            break;
        }
    }

    return server.is_shutting_down() ? 0 : 1;
}

} // namespace cppl::lsp
