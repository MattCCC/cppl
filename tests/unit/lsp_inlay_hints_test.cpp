// Inlay hints: the parameter each argument is passed to and the type `auto`
// deduced, from Clang, shown where the text they annotate was written
// (src/lsp/include/cppl/lsp/editor_view.hpp).

#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/lsp/transport.hpp"
#include "cppl/testing/test.hpp"

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace cppl::lsp;

namespace {

void expect(const std::string& actual, const std::string& expected, int line) {
    if (actual != expected) {
        ::cppl::testing::fail(__FILE__, line, "got '" + actual + "', expected '" + expected + "'");
    }
}

// Each hint as `line:character label`, for lines `first` through `last`.
std::string hints_of(const std::string& text, std::uint32_t first = 0, std::uint32_t last = 1000) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    TextDocumentItem item;
    item.uri = "file:///work/hints.cpp";
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);
    TextDocumentIdentifier id;
    id.uri = item.uri;
    const std::optional<std::vector<InlayHint>> hints =
        server.text_document_inlay_hint(id, Range{Position{first, 0}, Position{last, 0}});
    if (!hints.has_value()) {
        return "null";
    }
    std::string out;
    for (const InlayHint& hint : *hints) {
        out += (out.empty() ? "" : " | ") + std::to_string(hint.position.line) + ":" +
               std::to_string(hint.position.character) + " " + hint.label;
    }
    return out;
}

} // namespace

CPPL_TEST(each_argument_is_labelled_with_its_parameter) {
    expect(hints_of("int add(int left, int right);\n"
                    "int main() { return add(1, 2); }\n"),
           "1:24 left: | 1:27 right:", __LINE__);
}

CPPL_TEST(an_argument_that_says_its_parameter_or_is_not_written_is_not_labelled) {
    // The argument spells the parameter's name.
    expect(hints_of("int add(int left, int right);\n"
                    "int f(int left) { return add(left, 2); }\n"),
           "1:35 right:", __LINE__);
    // A default argument is written nowhere.
    expect(hints_of("int f(int a, int b = 2);\n"
                    "int g() { return f(1); }\n"),
           "1:19 a:", __LINE__);
    // An overloaded operator's operands are not arguments anyone names.
    expect(hints_of("struct V { V operator+(const V& other) const; };\n"
                    "V sum(V a, V b) { return a + b; }\n"),
           "", __LINE__);
    // A call a macro's body writes is annotated nowhere, not even at the
    // arguments the macro is given; a macro used as an argument is annotated
    // where it is used.
    expect(hints_of("int twice(int value);\n"
                    "#define PLUS_TWICE(v) (0 + twice(v))\n"
                    "#define ONE 1\n"
                    "int g() { return PLUS_TWICE(3) + twice(ONE); }\n"),
           "3:39 value:", __LINE__);
}

CPPL_TEST(a_variable_declared_auto_shows_what_it_was_deduced_as) {
    // A lambda's type has no name to show.
    expect(hints_of("double half() { auto x = 1.5; const auto& r = x; auto f = [] { return 1; }; return r + f(); }\n"),
           "0:22 : double | 0:43 : const double &", __LINE__);
}

CPPL_TEST(a_call_in_a_law_is_labelled_where_the_law_writes_it) {
    // The projection repeats the proposition in declarations it generates;
    // each argument is labelled once, where it is written, and nothing
    // generated is labelled.
    expect(hints_of("pure int scale(int amount, int factor) {\n"
                    "    return amount * factor;\n"
                    "}\n"
                    "law scale_by_one(int x)\n"
                    "    proves (scale(x, 1) == x);\n"),
           "4:18 amount: | 4:21 factor:", __LINE__);
}

CPPL_TEST(only_the_range_asked_for_is_labelled) {
    const std::string text = "int add(int left, int right);\n"
                             "int first() { return add(1, 2); }\n"
                             "int second() { return add(3, 4); }\n";
    expect(hints_of(text, 2, 3), "2:26 left: | 2:29 right:", __LINE__);
}

CPPL_TEST(inlay_hints_on_the_wire) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    const auto framed = [](const std::string& body) {
        return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    };
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") +
        framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
               R"({"uri":"file:///work/wire.cpp","languageId":"cpp","version":1,)"
               R"("text":"int add(int left, int right);\nint main() { return add(1, 2); }\n"}}})") +
        framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/inlayHint","params":{"textDocument":)"
               R"({"uri":"file:///work/wire.cpp"},"range":{"start":{"line":0,"character":0},)"
               R"("end":{"line":2,"character":0}}}})") +
        framed(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/inlayHint","params":{"textDocument":)"
               R"({"uri":"file:///work/wire.cpp"}}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;
    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    const std::string written = output.str();
    CPPL_CHECK(written.find(R"("inlayHintProvider":true)") != std::string::npos);
    CPPL_CHECK(written.find(R"("id":2,"result":[{"position":{"line":1,"character":24},"label":"left:","kind":2,)"
                            R"("paddingRight":true},)") != std::string::npos);
    CPPL_CHECK(written.find(R"("id":3,"error":{"code":-32602)") != std::string::npos);
}
