// Content-Length framing and the JSON-RPC dispatch loop, driven over
// in-memory streams rather than a real subprocess and pipes.

#include "cppl/lsp/server.hpp"
#include "cppl/lsp/transport.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <sstream>
#include <string>

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

CPPL_TEST(initialize_advertises_whole_document_tokens_for_every_kind_of_name) {
    Server server;
    std::istringstream input(framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") +
                             framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(
        output.str().find(R"("semanticTokensProvider":{"legend":{"tokenTypes":["namespace","type","class","enum",)"
                          R"("struct","typeParameter","parameter","variable","property","enumMember","function",)"
                          R"("method","macro","keyword"],"tokenModifiers":["declaration","readonly","static",)"
                          R"("deprecated","defaultLibrary"]},"full":true})") != std::string::npos);
}

CPPL_TEST(initialize_advertises_every_navigation_request) {
    Server server;
    std::istringstream input(framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") +
                             framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"("definitionProvider":true,"declarationProvider":true,)"
                                 R"("typeDefinitionProvider":true,"implementationProvider":true)") !=
               std::string::npos);
}

CPPL_TEST(a_definition_answers_with_locations) {
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
               R"({"uri":"file:///nav.cpp","languageId":"cpp","version":1,)"
               R"("text":"int answer = 42;\nint main() { return answer; }\n"}}})") +
        framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":)"
               R"({"uri":"file:///nav.cpp"},"position":{"line":1,"character":22}}})") +
        framed(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/declaration","params":{"textDocument":)"
               R"({"uri":"file:///never-opened.cpp"},"position":{"line":0,"character":0}}})") +
        framed(R"({"jsonrpc":"2.0","id":4,"method":"textDocument/typeDefinition","params":{"textDocument":)"
               R"({"uri":"file:///nav.cpp"}}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"("id":2,"result":[{"uri":"file:///nav.cpp","range":{"start":{"line":0,)"
                                 R"("character":4},"end":{"line":0,"character":10}}}])") != std::string::npos);
    CPPL_CHECK(output.str().find(R"("id":3,"result":null)") != std::string::npos);
    CPPL_CHECK(output.str().find(R"("id":4,"error":{"code":-32602)") != std::string::npos);
}

CPPL_TEST(references_and_highlights_answer_over_the_wire) {
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") +
        framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
               R"({"uri":"file:///refs.cpp","languageId":"cpp","version":1,)"
               R"("text":"int answer = 42;\nint main() { answer = 1; return answer; }\n"}}})") +
        framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/references","params":{"textDocument":)"
               R"({"uri":"file:///refs.cpp"},"position":{"line":1,"character":13},)"
               R"("context":{"includeDeclaration":false}}})") +
        framed(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/documentHighlight","params":{"textDocument":)"
               R"({"uri":"file:///refs.cpp"},"position":{"line":0,"character":4}}})") +
        framed(R"({"jsonrpc":"2.0","id":4,"method":"textDocument/references","params":{}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"("referencesProvider":true,"documentHighlightProvider":true)") != std::string::npos);
    CPPL_CHECK(output.str().find(R"("id":2,"result":[{"uri":"file:///refs.cpp","range":{"start":{"line":1,)"
                                 R"("character":13},"end":{"line":1,"character":19}}},{"uri":"file:///refs.cpp",)"
                                 R"("range":{"start":{"line":1,"character":32},"end":{"line":1,"character":38}}}])") !=
               std::string::npos);
    CPPL_CHECK(output.str().find(R"("id":3,"result":[{"range":{"start":{"line":0,"character":4},"end":{"line":0,)"
                                 R"("character":10}},"kind":1},{"range":{"start":{"line":1,"character":13},)"
                                 R"("end":{"line":1,"character":19}},"kind":3},{"range":{"start":{"line":1,)"
                                 R"("character":32},"end":{"line":1,"character":38}},"kind":2}])") !=
               std::string::npos);
    CPPL_CHECK(output.str().find(R"("id":4,"error":{"code":-32602)") != std::string::npos);
}

CPPL_TEST(a_code_lens_states_a_verdict_and_runs_nothing) {
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") +
        framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
               R"({"uri":"file:///lens.cpp","languageId":"cpp","version":1,)"
               R"("text":"pure unsigned same(unsigned x) {\n    return x;\n}\nlaw same_is_same(unsigned x)\n)"
               R"(    proves (same(x) == x);\n"}}})") +
        framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/codeLens","params":{"textDocument":)"
               R"({"uri":"file:///lens.cpp"}}})") +
        framed(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/codeLens","params":{}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"("codeLensProvider":{"resolveProvider":false})") != std::string::npos);
    CPPL_CHECK(output.str().find(R"("id":2,"result":[{"range":{"start":{"line":3,"character":4},)"
                                 R"("end":{"line":3,"character":16}},"command":{"title":"PROVEN","command":""}}])") !=
               std::string::npos);
    CPPL_CHECK(output.str().find(R"("id":3,"error":{"code":-32602)") != std::string::npos);
}

CPPL_TEST(a_completion_is_a_snippet_only_for_a_client_that_takes_snippets) {
    const auto completed = [](const std::string& capabilities) {
        Server server;
        std::istringstream input(
            framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":)" + capabilities + "}}") +
            framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
                   R"({"uri":"file:///complete.cpp","languageId":"cpp","version":1,)"
                   R"("text":"int twice(int value);\nint main() { return twi; }\n"}}})") +
            framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":)"
                   R"({"uri":"file:///complete.cpp"},"position":{"line":1,"character":23}}})") +
            framed(R"({"jsonrpc":"2.0","method":"exit"})"));
        std::ostringstream output;
        std::ostringstream log;
        [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
        return output.str();
    };
    const std::string snippets =
        completed(R"({"textDocument":{"completion":{"completionItem":{"snippetSupport":true}}}})");
    CPPL_CHECK(snippets.find(R"("triggerCharacters":["{",".",">",":"])") != std::string::npos);
    CPPL_CHECK(
        snippets.find(R"json("id":2,"result":{"isIncomplete":false,"items":[{"label":"twice(int value)",)json"
                      R"json("kind":3,"detail":"int","insertText":"twice(${1:int value})","insertTextFormat":2,)json"
                      R"json("filterText":"twice")json") != std::string::npos);
    const std::string plain = completed("{}");
    CPPL_CHECK(plain.find(R"("insertText":"twice","filterText":"twice")") != std::string::npos);
    CPPL_CHECK(plain.find(R"("insertTextFormat")") == std::string::npos);
}

CPPL_TEST(signature_help_answers_with_parameter_ranges) {
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") +
        framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
               R"({"uri":"file:///call.cpp","languageId":"cpp","version":1,)"
               R"("text":"int add(int a, int b);\nint main() { return add(1, ); }\n"}}})") +
        framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/signatureHelp","params":{"textDocument":)"
               R"({"uri":"file:///call.cpp"},"position":{"line":1,"character":27}}})") +
        framed(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/signatureHelp","params":{"textDocument":)"
               R"({"uri":"file:///call.cpp"},"position":{"line":0,"character":0}}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"json("signatureHelpProvider":{"triggerCharacters":["(",","],)json"
                                 R"json("retriggerCharacters":[")"]})json") != std::string::npos);
    CPPL_CHECK(output.str().find(R"json("id":2,"result":{"signatures":[{"label":"int add(int a, int b)",)json"
                                 R"json("parameters":[{"label":[8,13]},{"label":[15,20]}]}],"activeSignature":0,)json"
                                 R"json("activeParameter":1})json") != std::string::npos);
    CPPL_CHECK(output.str().find(R"("id":3,"result":null)") != std::string::npos);
}

CPPL_TEST(an_outline_is_nested_only_for_a_client_that_nests_it) {
    const auto outlined = [](const std::string& capabilities) {
        Server server;
        std::istringstream input(
            framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":)" + capabilities + "}}") +
            framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
                   R"({"uri":"file:///outline.cpp","languageId":"cpp","version":1,)"
                   R"("text":"namespace shapes {\nint area(int side);\n}\n"}}})") +
            framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/documentSymbol","params":{"textDocument":)"
                   R"({"uri":"file:///outline.cpp"}}})") +
            framed(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/documentSymbol","params":{}})") +
            framed(R"({"jsonrpc":"2.0","method":"exit"})"));
        std::ostringstream output;
        std::ostringstream log;
        [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
        return output.str();
    };
    const std::string nested =
        outlined(R"({"textDocument":{"documentSymbol":{"hierarchicalDocumentSymbolSupport":true}}})");
    CPPL_CHECK(nested.find(R"("documentSymbolProvider":{"label":"C++L"})") != std::string::npos);
    CPPL_CHECK(
        nested.find(R"json("id":2,"result":[{"name":"shapes","kind":3,)json"
                    R"json("range":{"start":{"line":0,"character":0},"end":{"line":2,"character":1}},)json"
                    R"json("selectionRange":{"start":{"line":0,"character":10},"end":{"line":0,"character":16}},)json"
                    R"json("children":[{"name":"area","detail":"int (int)","kind":12,)json") != std::string::npos);
    CPPL_CHECK(nested.find(R"("id":3,"error":{"code":-32602)") != std::string::npos);
    // A client that cannot nest is told each entry's container instead.
    const std::string flat = outlined("{}");
    CPPL_CHECK(
        flat.find(R"json("id":2,"result":[{"name":"shapes","kind":3,"location":{"uri":"file:///outline.cpp",)json") !=
        std::string::npos);
    CPPL_CHECK(flat.find(R"json({"name":"area","kind":12,"location":{"uri":"file:///outline.cpp","range":)json"
                         R"json({"start":{"line":1,"character":0},"end":{"line":1,"character":18}}},)json"
                         R"json("containerName":"shapes"})json") != std::string::npos);
}

CPPL_TEST(semantic_tokens_answer_with_the_encoded_tokens) {
    // `proof`, the proof's name as a declared function, `proves` and `refl`:
    // five integers each, positions relative to the token before.
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
               R"({"uri":"file:///st.cpp","languageId":"cpp","version":1,)"
               R"("text":"proof p()\n    proves (true)\n{\n    refl;\n}\n"}}})") +
        framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/semanticTokens/full","params":{"textDocument":)"
               R"({"uri":"file:///st.cpp"}}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"("id":2,"result":{"data":[0,0,5,13,0,0,6,1,10,1,1,4,6,13,0,2,4,4,13,0]})") !=
               std::string::npos);
}

CPPL_TEST(semantic_tokens_of_an_unknown_document_answer_null) {
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/semanticTokens/full","params":{"textDocument":)"
               R"({"uri":"file:///never-opened.cpp"}}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"("id":2,"result":null)") != std::string::npos);
}

CPPL_TEST(a_semantic_tokens_request_without_a_document_is_invalid_params) {
    Server server;
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/semanticTokens/full","params":{}})") +
        framed(R"({"jsonrpc":"2.0","id":4,"method":"textDocument/semanticTokens/full"})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;

    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"("id":3,"error":{"code":-32602)") != std::string::npos);
    CPPL_CHECK(output.str().find(R"("id":4,"error":{"code":-32602)") != std::string::npos);
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
    // A position with no decomposition under it is answered by Clang, with the
    // range of the name hovered.
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
    CPPL_CHECK(output.str().find(R"("id":2,"result":{"contents":{"kind":"markdown","value":"**function** `main`)") !=
               std::string::npos);
    CPPL_CHECK(output.str().find(R"("range":{"start":{"line":0,"character":4},"end":{"line":0,"character":8}})") !=
               std::string::npos);
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
