// The frontend reads preprocessed text, so positions come from line markers and
// literals must never be mistaken for code.

#include "cppl/frontend/token.hpp"
#include "cppl/testing/test.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

CPPL_TEST(a_marker_flagged_three_enters_a_system_header) {
    const std::string text = "# 1 \"main.cpp\"\n"
                             "int mine;\n"
                             "# 1 \"/usr/include/stdio.h\" 1 3 4\n"
                             "int theirs;\n"
                             "# 2 \"main.cpp\" 2\n"
                             "int again;\n";

    const TokenStream stream = cppl::frontend::lex(text, "main.cpp");

    CPPL_CHECK(!stream.is_system(find(stream, "mine")->file));
    CPPL_CHECK(stream.is_system(find(stream, "theirs")->file));
    CPPL_CHECK(!stream.is_system(find(stream, "again")->file));
}

CPPL_TEST(a_file_entered_by_a_marker_knows_the_include_that_entered_it) {
    // What `clang -E` writes for a buffer named by `#line` that includes a
    // header, which includes another.
    const std::string text = "# 1 \"/scratch/copy.cpp\"\n"
                             "# 1 \"<built-in>\" 1\n"
                             "# 1 \"<built-in>\" 3\n"
                             "# 554 \"<built-in>\" 3\n"
                             "# 1 \"<command line>\" 1\n"
                             "# 1 \"<built-in>\" 2\n"
                             "# 1 \"/scratch/copy.cpp\" 2\n"
                             "# 1 \"/work/doc.cpp\"\n"
                             "int before;\n"
                             "\n"
                             "# 1 \"/work/inner.hpp\" 1\n"
                             "\n"
                             "int inner_value;\n"
                             "# 1 \"/work/deep.hpp\" 1\n"
                             "\n"
                             "int deep_value = ;\n"
                             "# 4 \"/work/inner.hpp\" 2\n"
                             "# 4 \"/work/doc.cpp\" 2\n"
                             "int after;\n";

    const TokenStream stream = cppl::frontend::lex(text, "/work/doc.cpp");

    const std::optional<cppl::source::SourceLocation> inner = stream.included_at("/work/inner.hpp");
    CPPL_CHECK(inner.has_value());
    CPPL_CHECK_EQ(inner->file, std::string("/work/doc.cpp"));
    CPPL_CHECK_EQ(inner->line, 3u);

    const std::optional<cppl::source::SourceLocation> deep = stream.included_at("/work/deep.hpp");
    CPPL_CHECK(deep.has_value());
    CPPL_CHECK_EQ(deep->file, std::string("/work/inner.hpp"));
    CPPL_CHECK_EQ(deep->line, 3u);

    // The document itself was entered by no include, and a returning marker
    // records nothing.
    CPPL_CHECK(!stream.included_at("/work/doc.cpp").has_value());
    CPPL_CHECK_EQ(stream.location_of(*find(stream, "after")).line, 4u);
}

CPPL_TEST(tokens_take_the_columns_they_were_written_at) {
    // The preprocessor keeps a line's first column and writes every later run
    // of whitespace, and a comment, as one space.
    const std::string preprocessed = "# 1 \"main.cpp\"\n"
                                     "    refl; frobnicate a;\n"
                                     "int b ;\n";
    TokenStream stream = cppl::frontend::lex(preprocessed, "main.cpp");
    CPPL_CHECK_EQ(find(stream, "frobnicate")->column, 11u);

    stream.use_written_columns([](const std::string& file) -> std::optional<std::string> {
        CPPL_CHECK_EQ(file, std::string("main.cpp"));
        return std::string("    refl;      frobnicate a;\n"
                           "int /* the */ b\t;\n");
    });
    CPPL_CHECK_EQ(find(stream, "refl")->column, 5u);
    CPPL_CHECK_EQ(find(stream, "frobnicate")->column, 16u);
    CPPL_CHECK_EQ(find(stream, "a")->column, 27u);
    CPPL_CHECK_EQ(find(stream, "b")->column, 15u);
}

CPPL_TEST(a_macro_expansion_keeps_its_columns_and_the_line_around_it_is_written) {
    // Matched from the start up to the macro, and from the end back to it.
    const std::string preprocessed = "# 1 \"main.cpp\"\n"
                                     "int x = ((q) * 2) + y;\n";
    TokenStream stream = cppl::frontend::lex(preprocessed, "main.cpp");
    const std::uint32_t expanded = find(stream, "q")->column;

    stream.use_written_columns([](const std::string&) -> std::optional<std::string> {
        return std::string("int   x = TWICE(q)   +   y;\n");
    });
    CPPL_CHECK_EQ(find(stream, "x")->column, 7u);
    CPPL_CHECK_EQ(find(stream, "q")->column, expanded);
    CPPL_CHECK_EQ(find(stream, "+")->column, 22u);
    CPPL_CHECK_EQ(find(stream, "y")->column, 26u);
}

CPPL_TEST(system_headers_and_unreadable_files_keep_their_columns) {
    const std::string preprocessed = "# 1 \"main.cpp\"\n"
                                     "int a; int b;\n"
                                     "# 1 \"/usr/include/stdio.h\" 1 3 4\n"
                                     "int c; int d;\n"
                                     "# 2 \"main.cpp\" 2\n";
    TokenStream stream = cppl::frontend::lex(preprocessed, "main.cpp");

    std::vector<std::string> asked;
    stream.use_written_columns([&asked](const std::string& file) -> std::optional<std::string> {
        asked.push_back(file);
        return std::nullopt;
    });
    // A system header is never read, and a file that cannot be read changes nothing.
    CPPL_CHECK(asked == std::vector<std::string>{"main.cpp"});
    CPPL_CHECK_EQ(find(stream, "b")->column, 12u);
    CPPL_CHECK_EQ(find(stream, "d")->column, 12u);
}

CPPL_TEST(a_line_the_written_file_does_not_spell_keeps_its_columns) {
    // A file changed on disk after preprocessing is not trusted for columns.
    const std::string preprocessed = "# 1 \"main.cpp\"\n"
                                     "int first; int second;\n";
    TokenStream stream = cppl::frontend::lex(preprocessed, "main.cpp");

    stream.use_written_columns([](const std::string&) -> std::optional<std::string> {
        return std::string("long   other;   long   names;\n");
    });
    CPPL_CHECK_EQ(find(stream, "first")->column, 5u);
    CPPL_CHECK_EQ(find(stream, "second")->column, 16u);
}
