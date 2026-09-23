// Signature help: the declarations a call being written could resolve to, from
// Clang, and the argument being written (src/lsp/include/cppl/lsp/editor_view.hpp).

#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

using namespace cppl::lsp;

namespace {

// Opens `text` with `|` marking the cursor, and asks for signature help there.
std::optional<SignatureHelp> help_at(std::string text) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    const std::size_t cursor = text.find('|');
    if (cursor == std::string::npos) {
        ::cppl::testing::fail(__FILE__, __LINE__, "the text marks no cursor");
    }
    text.erase(cursor, 1);
    TextDocumentItem item;
    item.uri = "file:///work/main.cpp";
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);
    TextDocumentIdentifier id;
    id.uri = item.uri;
    return server.text_document_signature_help(id, PositionMapper(text).byte_offset_to_position(cursor));
}

// What each parameter of a signature spells.
std::string parameters(const SignatureInformation& signature) {
    std::string out;
    for (const auto& [start, end] : signature.parameters) {
        out += (out.empty() ? "" : "|") + signature.label.substr(start, end - start);
    }
    return out;
}

} // namespace

CPPL_TEST(a_call_shows_its_signature_and_the_argument_being_written) {
    const std::optional<SignatureHelp> help = help_at("int add(int left, int right);\n"
                                                      "int main() { return add(1, |); }\n");
    CPPL_CHECK(help.has_value());
    if (help.has_value()) {
        CPPL_CHECK_EQ(help->signatures.size(), std::size_t{1});
        CPPL_CHECK_EQ(help->signatures.front().label, std::string("int add(int left, int right)"));
        CPPL_CHECK_EQ(parameters(help->signatures.front()), std::string("int left|int right"));
        CPPL_CHECK_EQ(help->active_parameter, 1u);
    }
}

CPPL_TEST(every_overload_is_a_signature) {
    const std::optional<SignatureHelp> help = help_at("void put(int value);\n"
                                                      "void put(double value, int times);\n"
                                                      "int main() { put(|); }\n");
    CPPL_CHECK(help.has_value());
    if (help.has_value()) {
        CPPL_CHECK_EQ(help->signatures.size(), std::size_t{2});
        CPPL_CHECK_EQ(help->active_parameter, 0u);
        CPPL_CHECK(std::ranges::any_of(help->signatures, [](const SignatureInformation& signature) {
            return signature.label == "void put(double value, int times)";
        }));
    }
}

CPPL_TEST(a_nested_call_is_the_innermost_one) {
    const std::optional<SignatureHelp> help = help_at("int inner(int value);\n"
                                                      "int outer(int first, int second);\n"
                                                      "int main() { return outer(1, inner(|)); }\n");
    CPPL_CHECK(help.has_value());
    if (help.has_value()) {
        CPPL_CHECK_EQ(help->signatures.front().label, std::string("int inner(int value)"));
        CPPL_CHECK_EQ(help->active_parameter, 0u);
    }
}

CPPL_TEST(a_call_inside_a_law_is_helped_by_clang) {
    const std::optional<SignatureHelp> help = help_at("pure int scale(int amount, int factor) {\n"
                                                      "    return amount * factor;\n"
                                                      "}\n"
                                                      "law scale_by_one(int x)\n"
                                                      "    proves (scale(x, |) == x);\n");
    CPPL_CHECK(help.has_value());
    if (help.has_value()) {
        CPPL_CHECK_EQ(help->signatures.front().label, std::string("int scale(int amount, int factor)"));
        CPPL_CHECK_EQ(help->active_parameter, 1u);
    }
}

CPPL_TEST(outside_a_call_there_is_no_help) {
    CPPL_CHECK(!help_at("int main() { int x = 1|; return x; }\n").has_value());
    CPPL_CHECK(!help_at("int main() { if (true) { |return 0; } }\n").has_value());
    // Parentheses that are not a call's.
    CPPL_CHECK(!help_at("int main() { return (1 + |2); }\n").has_value());
    // A call already closed.
    CPPL_CHECK(!help_at("int add(int a, int b);\nint main() { return add(1, 2)|; }\n").has_value());
}

CPPL_TEST(the_active_signature_is_the_one_that_takes_the_argument_being_written) {
    const std::optional<SignatureHelp> help = help_at("void put(int value);\n"
                                                      "void put(double value, int times);\n"
                                                      "int main() { put(1.0, |); }\n");
    CPPL_CHECK(help.has_value());
    if (help.has_value()) {
        CPPL_CHECK_EQ(help->active_parameter, 1u);
        CPPL_CHECK(help->active_signature < help->signatures.size());
        if (help->active_signature < help->signatures.size()) {
            CPPL_CHECK_EQ(help->signatures[help->active_signature].label,
                          std::string("void put(double value, int times)"));
        }
    }
}
