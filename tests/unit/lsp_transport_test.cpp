// Content-Length framing and the JSON-RPC dispatch loop, driven over
// in-memory streams rather than a real subprocess and pipes.

#include "cppl/lsp/transport.hpp"
#include "cppl/testing/test.hpp"

#include <sstream>

using namespace cppl::lsp;

namespace {

std::string framed(const std::string& body) {
    std::ostringstream out;
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    return out.str();
}

// Splits a stream of framed messages on the "Content-Length:" header start
// so a test can count how many response/notification messages were
// written, without re-implementing a full parser here.
std::size_t count_messages(const std::string& text) {
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find("Content-Length:", position)) != std::string::npos) {
        ++count;
        ++position;
    }
    return count;
}

} // namespace

CPPL_TEST(read_message_parses_content_length_header) {
    std::istringstream input(framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})"));
    auto body = read_message(input);
    CPPL_CHECK(body.has_value());
    CPPL_CHECK_EQ(*body, R"({"jsonrpc":"2.0","id":1,"method":"initialize"})");
}

CPPL_TEST(read_message_returns_nullopt_at_end_of_stream) {
    std::istringstream input("");
    auto body = read_message(input);
    CPPL_CHECK(!body.has_value());
}

CPPL_TEST(read_message_returns_nullopt_on_truncated_body) {
    std::istringstream input("Content-Length: 100\r\n\r\n{\"short\":true}");
    auto body = read_message(input);
    CPPL_CHECK(!body.has_value());
}

CPPL_TEST(write_message_frames_with_content_length) {
    std::ostringstream output;
    write_message(output, R"({"ok":true})");
    CPPL_CHECK_EQ(output.str(), "Content-Length: 11\r\n\r\n{\"ok\":true}");
}

CPPL_TEST(initialize_request_gets_a_response) {
    Server server;
    std::istringstream input(framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") +
                             framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    const int exit_code = run_transport(server, input, output, log);

    CPPL_CHECK_EQ(exit_code, 1); // exit without a prior shutdown
    CPPL_CHECK(output.str().find("\"id\":1") != std::string::npos);
    CPPL_CHECK(output.str().find("capabilities") != std::string::npos);
}

CPPL_TEST(shutdown_then_exit_reports_success) {
    Server server;
    std::istringstream input(framed(R"({"jsonrpc":"2.0","id":1,"method":"shutdown"})") +
                             framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK_EQ(exit_code, 0);
}

CPPL_TEST(requests_after_shutdown_are_rejected) {
    Server server;
    std::istringstream input(framed(R"({"jsonrpc":"2.0","id":1,"method":"shutdown"})") +
                             framed(R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{}})") +
                             framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find("\"id\":2") != std::string::npos);
    CPPL_CHECK(output.str().find("\"error\"") != std::string::npos);
}

CPPL_TEST(unknown_method_gets_method_not_found_error) {
    Server server;
    std::istringstream input(framed(R"({"jsonrpc":"2.0","id":5,"method":"textDocument/hover","params":{}})") +
                             framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find("-32601") != std::string::npos);
}

CPPL_TEST(unknown_notification_is_silently_ignored) {
    Server server;
    std::istringstream input(framed(R"({"jsonrpc":"2.0","method":"$/setTrace","params":{}})") +
                             framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK_EQ(exit_code, 1);
    // No response is written for a notification, known or unknown.
    CPPL_CHECK(output.str().find("-32601") == std::string::npos);
}

CPPL_TEST(malformed_json_does_not_crash_the_session) {
    Server server;
    std::istringstream input(framed("{not valid json") + framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK_EQ(exit_code, 1);
    CPPL_CHECK(!log.str().empty());
}

CPPL_TEST(did_open_publishes_diagnostics_notification) {
    Server server;
    std::string open = framed(
        R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///test.cpp","languageId":"cppl","version":1,"text":"law bad(int x);"}}})");
    std::istringstream input(open + framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find("publishDiagnostics") != std::string::npos);
    CPPL_CHECK(output.str().find("file:///test.cpp") != std::string::npos);
}

CPPL_TEST(missing_method_on_request_gets_invalid_request_error) {
    Server server;
    std::istringstream input(framed(R"({"jsonrpc":"2.0","id":9})") + framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find("-32600") != std::string::npos);
}

CPPL_TEST(count_messages_helper_counts_frames) {
    std::string text = framed("a") + framed("b") + framed("c");
    CPPL_CHECK_EQ(count_messages(text), 3u);
}
