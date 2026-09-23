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
    // `textDocument/rename` stands for a method this server does not
    // implement. It must stay one the server does not handle: this test
    // previously used `textDocument/hover` and went stale the moment hover
    // was implemented.
    Server server;
    std::istringstream input(framed(R"({"jsonrpc":"2.0","id":5,"method":"textDocument/rename","params":{}})") +
                             framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find("-32601") != std::string::npos);
}

CPPL_TEST(initialize_advertises_completion_and_hover) {
    Server server;
    std::istringstream input(framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") +
                             framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    // An editor is told what the server can do; both are implemented, so both
    // are advertised (tools/cppl-lsp/README.md).
    CPPL_CHECK(output.str().find("completionProvider") != std::string::npos);
    CPPL_CHECK(output.str().find("hoverProvider") != std::string::npos);
}

CPPL_TEST(initialize_advertises_both_code_action_kinds) {
    Server server;
    std::istringstream input(framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") +
                             framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"("codeActionProvider":{"codeActionKinds":["quickfix","source.fixAll.cppl"]})") !=
               std::string::npos);
}

CPPL_TEST(a_code_action_carries_its_edit_as_a_workspace_edit) {
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
               R"({"uri":"file:///ca.cpp","languageId":"cpp","version":1,)"
               R"("text":"int identity(int x) { return x; }\nlaw l(int x) ensures (identity(x) == x);\n"}}})") +
        framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/codeAction","params":{"textDocument":)"
               R"({"uri":"file:///ca.cpp"},"range":{"start":{"line":1,"character":15},)"
               R"("end":{"line":1,"character":15}},"context":{"diagnostics":[],"only":["quickfix"]}}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"("title":"Replace Law 'ensures' with 'proves'","kind":"quickfix",)"
                                 R"("edit":{"changes":{"file:///ca.cpp":[{"range":{"start":{"line":1,"character":13},)"
                                 R"("end":{"line":1,"character":20}},"newText":"proves"}]}})") != std::string::npos);
}

CPPL_TEST(a_code_action_request_without_a_range_is_invalid_params) {
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/codeAction","params":{"textDocument":)"
               R"({"uri":"file:///ca.cpp"},"context":{"diagnostics":[]}}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"("id":3,"error":{"code":-32602)") != std::string::npos);
}

CPPL_TEST(hover_outside_a_case_block_is_answered_not_rejected) {
    // A position with no decomposition under it is an ordinary answer, not a
    // failure: ordinary C++ hover belongs to clangd.
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") +
        framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
               R"({"uri":"file:///hover.cpp","languageId":"cpp","version":1,"text":"int main() { return 0; }"}}})") +
        framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":)"
               R"({"uri":"file:///hover.cpp"},"position":{"line":0,"character":4}}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find("-32601") == std::string::npos);
    CPPL_CHECK(output.str().find("-32602") == std::string::npos);
}

CPPL_TEST(a_negative_position_is_rejected_not_wrapped) {
    // Positions arrive as JSON doubles from an untrusted client. Narrowing a
    // negative one to the unsigned position type is undefined behavior, and
    // clamping it would answer for a position nobody asked about.
    Server server;
    std::istringstream input(framed(R"({"jsonrpc":"2.0","id":1,"method":"textDocument/hover","params":{"textDocument":)"
                                    R"({"uri":"file:///neg.cpp"},"position":{"line":-1,"character":0}}})") +
                             framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find("-32602") != std::string::npos);
}

CPPL_TEST(every_client_number_is_checked_before_it_is_narrowed) {
    // Positions, ranges and versions all arrive as JSON doubles, and each is
    // checked wherever it is read: narrowing one that does not fit is
    // undefined behavior, which the UBSan configuration traps.
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","id":1,"method":"textDocument/onTypeFormatting","params":{"textDocument":)"
               R"({"uri":"file:///n.cpp"},"position":{"line":-1,"character":0},"ch":";"}})") +
        framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/rangeFormatting","params":{"textDocument":)"
               R"({"uri":"file:///n.cpp"},"range":{"start":{"line":0,"character":-3},)"
               R"("end":{"line":0,"character":1}}}})") +
        framed(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/rangeFormatting","params":{"textDocument":)"
               R"({"uri":"file:///n.cpp"},"range":{"start":{"line":0,"character":0},)"
               R"("end":{"line":1e300,"character":1}}}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"("id":1,"error":{"code":-32602)") != std::string::npos);
    CPPL_CHECK(output.str().find(R"("id":2,"error":{"code":-32602)") != std::string::npos);
    CPPL_CHECK(output.str().find(R"("id":3,"error":{"code":-32602)") != std::string::npos);
}

CPPL_TEST(a_range_that_ends_before_it_starts_is_malformed) {
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","id":1,"method":"textDocument/rangeFormatting","params":{"textDocument":)"
               R"({"uri":"file:///n.cpp"},"range":{"start":{"line":2,"character":0},)"
               R"("end":{"line":1,"character":9}}}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"("id":1,"error":{"code":-32602)") != std::string::npos);
}

CPPL_TEST(an_out_of_range_version_costs_the_version_not_the_text) {
    // The version is only recorded, so one no `std::int32_t` holds is logged
    // and replaced by 0. The document itself is opened and changed as sent.
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
               R"({"uri":"file:///v.cpp","languageId":"cpp","version":1e20,"text":"int x;"}}})") +
        framed(R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":)"
               R"({"uri":"file:///v.cpp","version":-1e20},"contentChanges":[{"text":"int y;"}]}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(log.str().find("textDocument/didOpen has an out-of-range 'version'") != std::string::npos);
    CPPL_CHECK(log.str().find("textDocument/didChange has an out-of-range 'version'") != std::string::npos);
    // Opening and then changing the document each published its diagnostics.
    CPPL_CHECK_EQ(count_messages(output.str()), 2u);
}

CPPL_TEST(a_change_with_a_malformed_range_is_skipped_not_taken_as_the_document) {
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
               R"({"uri":"file:///r.cpp","languageId":"cpp","version":1,"text":"int x;"}}})") +
        framed(R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":)"
               R"({"uri":"file:///r.cpp","version":2},"contentChanges":[{"range":{"start":{"line":-1,)"
               R"("character":0},"end":{"line":0,"character":0}},"text":"garbage"}]}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(log.str().find("skipping a change with a malformed 'range'") != std::string::npos);
    // Only the open published: a change with nothing applicable left in it
    // changes nothing and publishes nothing.
    CPPL_CHECK_EQ(count_messages(output.str()), 1u);
}

CPPL_TEST(completion_without_a_position_is_an_invalid_params_error) {
    // A malformed request is rejected rather than answered with an empty
    // list, which would be indistinguishable from "nothing to suggest".
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","id":1,"method":"textDocument/completion","params":{"textDocument":)"
               R"({"uri":"file:///none.cpp"}}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find("-32602") != std::string::npos);
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
