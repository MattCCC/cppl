// Compiles in the background: requests are answered while a compile runs, a
// change is compiled once typing pauses, a withdrawn request is not run, and
// each compile is reported as work in progress (src/lsp/include/cppl/lsp/
// transport.hpp, TransportOptions).

#include "cppl/driver/scratch.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/lsp/transport.hpp"
#include "cppl/lsp/uri.hpp"
#include "cppl/testing/test.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <istream>
#include <mutex>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

using namespace cppl::lsp;

namespace {

// An input that yields what has been written to it and waits for more, until
// it is closed: an editor's end of the pipe.
class Pipe : public std::streambuf {
  public:
    void send(const std::string& body) {
        {
            const std::scoped_lock lock(mutex_);
            pending_ += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        }
        arrived_.notify_all();
    }

    void close() {
        {
            const std::scoped_lock lock(mutex_);
            closed_ = true;
        }
        arrived_.notify_all();
    }

  protected:
    int_type underflow() override {
        std::unique_lock lock(mutex_);
        arrived_.wait(lock, [this] { return !pending_.empty() || closed_; });
        if (pending_.empty()) {
            return traits_type::eof();
        }
        current_ = std::move(pending_);
        pending_.clear();
        setg(current_.data(), current_.data(), current_.data() + current_.size());
        return traits_type::to_int_type(current_.front());
    }

  private:
    std::mutex mutex_;
    std::condition_variable arrived_;
    std::string pending_;
    std::string current_;
    bool closed_ = false;
};

// What the server has written, read while it writes.
class Watched : public std::streambuf {
  public:
    // Whether `needle` appears within `timeout`, looking from `from`; where
    // it ends, if it does.
    std::size_t wait_for(std::string_view needle, std::size_t from = 0,
                         std::chrono::seconds timeout = std::chrono::seconds(60)) {
        std::unique_lock lock(mutex_);
        std::size_t found = std::string::npos;
        written_.wait_for(lock, timeout, [&] {
            found = text_.find(needle, from);
            return found != std::string::npos;
        });
        return found == std::string::npos ? std::string::npos : found + needle.size();
    }

    std::string text() {
        const std::scoped_lock lock(mutex_);
        return text_;
    }

  protected:
    int_type overflow(int_type character) override {
        if (character != traits_type::eof()) {
            const std::scoped_lock lock(mutex_);
            text_.push_back(traits_type::to_char_type(character));
        }
        written_.notify_all();
        return character;
    }

    std::streamsize xsputn(const char* data, std::streamsize count) override {
        {
            const std::scoped_lock lock(mutex_);
            text_.append(data, static_cast<std::size_t>(count));
        }
        written_.notify_all();
        return count;
    }

  private:
    std::mutex mutex_;
    std::condition_variable written_;
    std::string text_;
};

// A session with an editor on the other end, which runs until `finish`.
class Session {
  public:
    // `more` is further initialize parameters, each with a comma before it.
    explicit Session(std::chrono::milliseconds quiet, const std::string& capabilities = "{}",
                     const std::string& more = "")
        : input_(&pipe_),
          output_(&watched_) {
        TransportOptions options;
        options.background_compiles = true;
        options.quiet = quiet;
        thread_ = std::thread([this, options] { exit_code_ = run_transport(server_, input_, output_, log_, options); });
        pipe_.send(R"({"jsonrpc":"2.0","id":"init","method":"initialize","params":{"capabilities":)" + capabilities +
                   more + "}}");
        CPPL_CHECK(watched_.wait_for(R"("id":"init","result")") != std::string::npos);
        pipe_.send(R"({"jsonrpc":"2.0","method":"initialized","params":{}})");
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    // A test that failed before finishing still ends its session.
    ~Session() {
        try {
            finish();
        } catch (...) { // NOLINT(bugprone-empty-catch): a destructor has nowhere to report to
        }
    }

    void send(const std::string& body) {
        pipe_.send(body);
    }

    Watched& output() {
        return watched_;
    }

    void open(const std::string& uri, const std::string& text_json) {
        send(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" + uri +
             R"(","languageId":"cpp","version":1,"text":)" + text_json + "}}}");
    }

    void change(const std::string& uri, int version, const std::string& text_json) {
        send(R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")" + uri +
             R"(","version":)" + std::to_string(version) + R"(},"contentChanges":[{"text":)" + text_json + "}]}}");
    }

    int finish() {
        if (thread_.joinable()) {
            send(R"({"jsonrpc":"2.0","id":"down","method":"shutdown"})");
            send(R"({"jsonrpc":"2.0","method":"exit"})");
            pipe_.close();
            thread_.join();
        }
        return exit_code_;
    }

  private:
    Pipe pipe_;
    Watched watched_;
    std::istream input_;
    std::ostream output_;
    std::ostringstream log_;
    Server server_{CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"}};
    int exit_code_ = -1;
    std::thread thread_;
};

void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

std::size_t count(const std::string& text, std::string_view needle) {
    std::size_t found = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        ++found;
    }
    return found;
}

} // namespace

CPPL_TEST(an_opened_document_is_compiled_in_the_background_and_its_diagnostics_published) {
    Session session(std::chrono::milliseconds(300));
    session.open("file:///work/open.cpp", R"("int main() { return undeclared; }\n")");
    CPPL_CHECK(session.output().wait_for("publishDiagnostics") != std::string::npos);
    CPPL_CHECK(session.output().wait_for("undeclared") != std::string::npos);
    CPPL_CHECK_EQ(session.finish(), 0);
}

CPPL_TEST(a_request_is_answered_while_a_change_waits_to_be_compiled) {
    // The change's compile waits for typing to pause for a minute; the hover
    // sent after it is answered long before.
    Session session(std::chrono::milliseconds(60000));
    session.open("file:///work/wait.cpp", R"("int answer = 42;\nint main() { return answer; }\n")");
    const std::size_t opened = session.output().wait_for("publishDiagnostics");
    CPPL_CHECK(opened != std::string::npos);
    session.change("file:///work/wait.cpp", 2,
                   R"("int answer = 42;\nint main() { return answer; }\nint broken = missing;\n")");
    session.send(R"({"jsonrpc":"2.0","id":7,"method":"textDocument/hover","params":{"textDocument":)"
                 R"({"uri":"file:///work/wait.cpp"},"position":{"line":1,"character":20}}})");
    const std::size_t answered = session.output().wait_for(R"("id":7,"result":{"contents")", opened);
    CPPL_CHECK(answered != std::string::npos);
    // The change is not compiled while typing has not paused: no second
    // publication, however long one waits for it here.
    CPPL_CHECK(session.output().wait_for("publishDiagnostics", opened, std::chrono::seconds(5)) == std::string::npos);
    CPPL_CHECK_EQ(session.finish(), 0);
}

CPPL_TEST(changes_made_while_typing_are_compiled_once_typing_pauses) {
    Session session(std::chrono::milliseconds(500));
    session.open("file:///work/typing.cpp", R"("int a = 1;\n")");
    const std::size_t opened = session.output().wait_for("publishDiagnostics");
    CPPL_CHECK(opened != std::string::npos);
    session.change("file:///work/typing.cpp", 2, R"("int a = 1;\nint b = c;\n")");
    session.change("file:///work/typing.cpp", 3, R"("int a = 1;\nint b = cc;\n")");
    session.change("file:///work/typing.cpp", 4, R"("int a = 1;\nint b = ccc;\n")");
    CPPL_CHECK(session.output().wait_for("'ccc'", opened) != std::string::npos);
    // One compile, of the last text: nothing about `c` or `cc` was published.
    const std::string after = session.output().text().substr(opened);
    CPPL_CHECK_EQ(count(after, "publishDiagnostics"), std::size_t{1});
    CPPL_CHECK(after.find("'c'") == std::string::npos);
    CPPL_CHECK(after.find("'cc'") == std::string::npos);
    CPPL_CHECK_EQ(session.finish(), 0);
}

CPPL_TEST(a_compile_of_text_since_edited_is_dropped_not_published) {
    // The open's compile is still running when the change arrives, so what it
    // found is about text no longer there, and only the change's is published.
    Session session(std::chrono::milliseconds(200));
    session.open("file:///work/stale.cpp", R"("int first = undefined_first;\n")");
    session.change("file:///work/stale.cpp", 2, R"("int second = undefined_second;\n")");
    CPPL_CHECK(session.output().wait_for("undefined_second") != std::string::npos);
    CPPL_CHECK_EQ(session.finish(), 0);
    const std::string written = session.output().text();
    CPPL_CHECK_EQ(count(written, "publishDiagnostics"), std::size_t{1});
    CPPL_CHECK(written.find("undefined_first") == std::string::npos);
}

CPPL_TEST(a_request_withdrawn_before_its_turn_is_answered_as_cancelled) {
    Session session(std::chrono::milliseconds(300));
    session.open("file:///work/cancel.cpp", R"("#include <vector>\nint main() { std::vector<int> v; return 0; }\n")");
    // The first request makes the server parse the document, which takes
    // long enough for the next two to arrive while it does.
    session.send(R"({"jsonrpc":"2.0","id":1,"method":"textDocument/hover","params":{"textDocument":)"
                 R"({"uri":"file:///work/cancel.cpp"},"position":{"line":1,"character":18}}})"
                 "\r\n");
    session.send(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":)"
                 R"({"uri":"file:///work/cancel.cpp"},"position":{"line":1,"character":30}}})");
    session.send(R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":2}})");
    // A withdrawal of a request answered long ago, or never made, is ignored.
    session.send(R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":99}})");
    CPPL_CHECK(session.output().wait_for(R"("id":1,"result")") != std::string::npos);
    CPPL_CHECK(session.output().wait_for(R"("id":2,"error":{"code":-32800)") != std::string::npos);
    CPPL_CHECK_EQ(session.finish(), 0);
    CPPL_CHECK(session.output().text().find(R"("id":99)") == std::string::npos);
}

CPPL_TEST(each_compile_is_reported_as_progress_on_a_token_the_client_created) {
    Session session(std::chrono::milliseconds(300),
                    R"({"window":{"workDoneProgress":true},"workspace":{"codeLens":{"refreshSupport":true},)"
                    R"("semanticTokens":{"refreshSupport":true}}})");
    // The server asks for a token; the client creates it.
    CPPL_CHECK(
        session.output().wait_for(
            R"("id":"cppl/create/1","method":"window/workDoneProgress/create","params":{"token":"cppl/progress/1"})") !=
        std::string::npos);
    session.send(R"({"jsonrpc":"2.0","id":"cppl/create/1","result":null})");
    session.open("file:///work/progress.cpp", R"("int main() { return 0; }\n")");
    const std::size_t begun = session.output().wait_for(
        R"("method":"$/progress","params":{"token":"cppl/progress/1","value":{"kind":"begin","title":"Checking",)"
        R"("message":"progress.cpp")");
    CPPL_CHECK(begun != std::string::npos);
    CPPL_CHECK(session.output().wait_for(R"("params":{"token":"cppl/progress/1","value":{"kind":"end"}})", begun) !=
               std::string::npos);
    // The token taken, the next one is asked for; and the client is told its
    // lenses and tokens may have changed.
    CPPL_CHECK(session.output().wait_for(R"("id":"cppl/create/2","method":"window/workDoneProgress/create")") !=
               std::string::npos);
    CPPL_CHECK(session.output().wait_for(R"("method":"workspace/codeLens/refresh")") != std::string::npos);
    CPPL_CHECK(session.output().wait_for(R"("method":"workspace/semanticTokens/refresh")") != std::string::npos);
    // The client's answers to the server's requests are not requests to answer.
    session.send(R"({"jsonrpc":"2.0","id":"cppl/refresh/3","result":null})");
    CPPL_CHECK_EQ(session.finish(), 0);
    CPPL_CHECK(session.output().text().find(R"("id":"cppl/refresh/3","error")") == std::string::npos);
}

CPPL_TEST(indexing_the_workspace_is_reported_as_progress_and_answers_workspace_symbols) {
    const cppl::driver::ScratchDirectory scratch;
    write(scratch.path() / "first.cpp", "int indexed_first();\n");
    Session session(std::chrono::milliseconds(300), R"({"window":{"workDoneProgress":true}})",
                    R"(,"rootUri":")" + path_to_uri(scratch.path().string()) + R"(")");
    // A token for compiles, and one for indexing.
    CPPL_CHECK(session.output().wait_for(R"("id":"cppl/create/1","method":"window/workDoneProgress/create")") !=
               std::string::npos);
    CPPL_CHECK(session.output().wait_for(R"("id":"cppl/create/2","method":"window/workDoneProgress/create")") !=
               std::string::npos);
    session.send(R"({"jsonrpc":"2.0","id":"cppl/create/1","result":null})");
    session.send(R"({"jsonrpc":"2.0","id":"cppl/create/2","result":null})");
    // Answered in turn, after the tokens are created, once the first file is
    // indexed.
    int asked = 0;
    const auto symbols = [&session, &asked] {
        const std::string id = std::to_string(++asked);
        session.send(R"({"jsonrpc":"2.0","id":)" + id +
                     R"(,"method":"workspace/symbol","params":{"query":"indexed_"}})");
        const std::size_t answered = session.output().wait_for(R"("id":)" + id + ",");
        return answered == std::string::npos ? std::string() : session.output().text().substr(answered);
    };
    constexpr int kAsks = 600;
    std::string answer = symbols();
    for (int tries = 1; tries < kAsks && answer.starts_with(R"("result":)") &&
                        !answer.starts_with(R"("result":[{"name":"indexed_first")");
         ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        answer = symbols();
    }
    CPPL_CHECK(answer.starts_with(R"("result":[{"name":"indexed_first","kind":12,"location":{"uri":")"));
    const std::size_t indexed = session.output().text().size();

    // A file added is indexed on a token, file by file.
    write(scratch.path() / "second.cpp", "int indexed_second();\n");
    const std::size_t begun = session.output().wait_for(
        R"("value":{"kind":"begin","title":"Indexing","message":"0/1 files","percentage":0,)", indexed);
    CPPL_CHECK(begun != std::string::npos);
    CPPL_CHECK(session.output().wait_for(R"("value":{"kind":"end"}})", begun) != std::string::npos);
    // The token taken, the next one is asked for.
    CPPL_CHECK(session.output().wait_for(R"("id":"cppl/create/3","method":"window/workDoneProgress/create")") !=
               std::string::npos);
    CPPL_CHECK(symbols().starts_with(R"("result":[{"name":"indexed_first",)"));
    CPPL_CHECK(session.output().text().find(R"({"name":"indexed_second","kind":12,"location":{"uri":")", begun) !=
               std::string::npos);
    CPPL_CHECK_EQ(session.finish(), 0);
}

CPPL_TEST(without_progress_support_no_progress_is_reported) {
    Session session(std::chrono::milliseconds(300));
    session.open("file:///work/quiet.cpp", R"("int main() { return 0; }\n")");
    CPPL_CHECK(session.output().wait_for("publishDiagnostics") != std::string::npos);
    CPPL_CHECK_EQ(session.finish(), 0);
    const std::string written = session.output().text();
    CPPL_CHECK(written.find("workDoneProgress/create") == std::string::npos);
    CPPL_CHECK(written.find("$/progress") == std::string::npos);
    CPPL_CHECK(written.find("refresh") == std::string::npos);
}
