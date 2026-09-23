// The frontend reads preprocessed text, so positions come from line markers and
// literals must never be mistaken for code.

#include "cppl/frontend/token.hpp"
#include "cppl/testing/test.hpp"

#include <string>
#include <string_view>

namespace {

using cppl::frontend::Token;
using cppl::frontend::TokenKind;
using cppl::frontend::TokenStream;

const Token* find(const TokenStream& stream, std::string_view text) {
    for (const Token& token : stream.tokens()) {
        if (token.text == text) {
            return &token;
        }
    }
    return nullptr;
}

} // namespace

CPPL_TEST(line_markers_give_tokens_their_user_visible_position) {
    const std::string text = "# 1 \"main.cpp\"\n"
                             "int first;\n"
                             "# 10 \"payment.hpp\"\n"
                             "int second;\n";

    const TokenStream stream = cppl::frontend::lex(text, "main.cpp");

    const Token* first = find(stream, "first");
    const Token* second = find(stream, "second");
    CPPL_CHECK(first != nullptr);
    CPPL_CHECK(second != nullptr);

    CPPL_CHECK_EQ(stream.location_of(*first).file, std::string("main.cpp"));
    CPPL_CHECK_EQ(stream.location_of(*first).line, 1u);
    CPPL_CHECK_EQ(stream.location_of(*second).file, std::string("payment.hpp"));
    CPPL_CHECK_EQ(stream.location_of(*second).line, 10u);
}

CPPL_TEST(text_inside_literals_is_not_code) {
    const std::string text = "const char* message = \"law f(int x) proves (y);\";\n";

    const TokenStream stream = cppl::frontend::lex(text, "main.cpp");

    CPPL_CHECK(find(stream, "ensures") == nullptr);
    CPPL_CHECK(find(stream, "law") == nullptr);

    bool saw_string = false;
    for (const Token& token : stream.tokens()) {
        saw_string = saw_string || token.kind == TokenKind::StringLiteral;
    }
    CPPL_CHECK(saw_string);
}

CPPL_TEST(raw_strings_are_consumed_whole) {
    const std::string text = "auto raw = R\"sql(law x() proves (1);)sql\";\nint after;\n";

    const TokenStream stream = cppl::frontend::lex(text, "main.cpp");

    CPPL_CHECK(find(stream, "ensures") == nullptr);
    CPPL_CHECK(find(stream, "after") != nullptr);
}

CPPL_TEST(comments_are_skipped) {
    const std::string text = "// law commented(int x) proves (x == x);\n"
                             "/* law blocked(int x) proves (x == x); */\n"
                             "int value;\n";

    const TokenStream stream = cppl::frontend::lex(text, "main.cpp");

    CPPL_CHECK(find(stream, "commented") == nullptr);
    CPPL_CHECK(find(stream, "blocked") == nullptr);
    CPPL_CHECK(find(stream, "value") != nullptr);
}

CPPL_TEST(token_spans_address_the_scanned_buffer) {
    const std::string text = "int value = 1;\n";

    const TokenStream stream = cppl::frontend::lex(text, "main.cpp");

    const Token* value = find(stream, "value");
    CPPL_CHECK(value != nullptr);
    CPPL_CHECK_EQ(text.substr(value->span.offset, value->span.length), std::string("value"));
    CPPL_CHECK_EQ(value->column, 5u);
}
