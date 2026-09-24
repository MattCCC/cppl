// Semantic tokens: every name as the kind of thing it names, C++ from Clang and
// C++L's words and names from the recognizer and the compile
// (src/lsp/include/cppl/lsp/semantic_tokens.hpp).

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/semantic_tokens.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace cppl;
using namespace cppl::lsp;

namespace {

void expect(const std::string& actual, const std::string& expected, int line) {
    if (actual != expected) {
        ::cppl::testing::fail(__FILE__, line, "got '" + actual + "', expected '" + expected + "'");
    }
}

struct Decoded {
    std::uint32_t line = 0;
    std::uint32_t character = 0;
    std::uint32_t length = 0;
    std::uint32_t type = 0;
    std::uint32_t modifiers = 0;
};

std::vector<Decoded> decode(const std::vector<std::uint32_t>& data) {
    CPPL_CHECK_EQ(data.size() % 5, std::size_t{0});
    std::vector<Decoded> tokens;
    Decoded at;
    for (std::size_t index = 0; index + 5 <= data.size(); index += 5) {
        at.character = data[index] == 0 ? at.character + data[index + 1] : data[index + 1];
        at.line += data[index];
        at.length = data[index + 2];
        at.type = data[index + 3];
        at.modifiers = data[index + 4];
        tokens.push_back(at);
    }
    return tokens;
}

// Each token as `line:character:length:type`, then `+modifier` for each
// modifier, in absolute coordinates.
std::string described(const std::vector<std::uint32_t>& data, bool keywords_only = false) {
    std::string out;
    for (const Decoded& token : decode(data)) {
        const bool keyword = token.type == static_cast<std::uint32_t>(TokenType::Keyword);
        if (keywords_only && !keyword) {
            continue;
        }
        out += (out.empty() ? "" : " ") + std::to_string(token.line) + ":" + std::to_string(token.character) + ":" +
               std::to_string(token.length);
        if (!keywords_only) {
            out += ":" + std::string(kTokenTypes.at(token.type));
            for (std::size_t bit = 0; bit < kTokenModifiers.size(); ++bit) {
                if ((token.modifiers & (1U << bit)) != 0) {
                    out += "+" + std::string(kTokenModifiers.at(bit));
                }
            }
        }
    }
    return out;
}

// C++L's tokens for `text`, read by the recognizer alone.
std::vector<std::uint32_t> cppl_data(const std::string& text, bool path_claims_recognized,
                                     bool path_splits_recognized = false) {
    const frontend::TokenStream stream = frontend::lex(text, "main.cpp");
    diagnostics::Engine engine;
    const frontend::Syntax syntax = frontend::recognize(stream, engine);
    return encode(cppl_tokens(syntax, path_claims_recognized, path_splits_recognized), text);
}

std::string keywords_of(const std::string& text, bool path_claims_recognized, bool path_splits_recognized = false) {
    return described(cppl_data(text, path_claims_recognized, path_splits_recognized), true);
}

std::string read_fixture(const std::string& name) {
    const std::string path = std::string(CPPL_TEST_FIXTURES_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        ::cppl::testing::fail(__FILE__, __LINE__, "could not read fixture '" + path + "'");
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// A server that compiles its buffers with the fixtures' include path, as an
// editor launching it for this repository would configure it.
Server fixture_server(std::string clang = CPPL_TEST_DEFAULT_CLANG) {
    return Server(std::move(clang), {"-std=c++20", "-I" + std::string(CPPL_TEST_FIXTURES_DIR)});
}

void open(Server& server, const std::string& uri, std::string text) {
    TextDocumentItem item;
    item.uri = uri;
    item.text = std::move(text);
    item.version = 1;
    server.text_document_did_open(item);
}

std::vector<std::uint32_t> server_data(Server& server, const std::string& uri) {
    TextDocumentIdentifier id;
    id.uri = uri;
    const std::optional<std::vector<std::uint32_t>> data = server.text_document_semantic_tokens(id);
    CPPL_CHECK(data.has_value());
    return data.value_or(std::vector<std::uint32_t>{});
}

// How many keyword tokens spell one of `words` in `text`. Every line these
// fixtures color is ASCII, so a character is a byte.
std::size_t keywords_spelling(const std::string& text, const std::vector<std::uint32_t>& data,
                              const std::vector<std::string>& words) {
    std::vector<std::size_t> line_starts = {0};
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (text[at] == '\n') {
            line_starts.push_back(at + 1);
        }
    }
    return static_cast<std::size_t>(std::ranges::count_if(decode(data), [&](const Decoded& token) {
        return token.type == static_cast<std::uint32_t>(TokenType::Keyword) &&
               std::ranges::find(words, text.substr(line_starts[token.line] + token.character, token.length)) !=
                   words.end();
    }));
}

} // namespace

CPPL_TEST(every_cppl_word_and_the_names_cppl_declares_are_tokens) {
    // Without a compile, a name a statement uses is left to the grammar; the
    // name `assume` binds is a constant it declares.
    expect(described(cppl_data("proof p(int a)\n"
                               "    proves (a == a)\n"
                               "{\n"
                               "    assume h : a == a;\n"
                               "    rewrite h;\n"
                               "    apply h;\n"
                               "    exact h;\n"
                               "    contradiction h;\n"
                               "    induction a;\n"
                               "    refl;\n"
                               "}\n",
                               false)),
           "0:0:5:keyword 0:6:1:function+declaration 1:4:6:keyword 3:4:6:keyword 3:11:1:variable+declaration+readonly "
           "4:4:7:keyword 5:4:5:keyword 6:4:5:keyword 7:4:13:keyword 8:4:9:keyword 9:4:4:keyword",
           __LINE__);
    expect(described(cppl_data("trusted law holds(int x)\n"
                               "    expects (x > 0)\n"
                               "    proves (x == x);\n"
                               "type Small = int where (self < 10);\n"
                               "verified pure int keep(int x)\n"
                               "    ensures (result == x)\n"
                               "{\n"
                               "    while (x > 0) invariant (x >= 0) decreases (x) { --x; }\n"
                               "    return x;\n"
                               "}\n",
                               false)),
           "0:0:7:keyword 0:8:3:keyword 0:12:5:function+declaration 1:4:7:keyword 2:4:6:keyword 3:0:4:keyword "
           "3:5:5:type+declaration 3:17:5:keyword 4:0:8:keyword 4:9:4:keyword 5:4:7:keyword 7:18:9:keyword "
           "7:37:9:keyword",
           __LINE__);
}

CPPL_TEST(statements_inside_arms_and_omissions_are_tokens) {
    expect(keywords_of("proof q(State s)\n"
                       "    proves (true)\n"
                       "{\n"
                       "    cases s {\n"
                       "        omit State::idle by contradiction never;\n"
                       "        State::busy => { refl; }\n"
                       "    }\n"
                       "}\n",
                       false),
           "0:0:5 1:4:6 3:4:5 4:8:4 4:25:2 4:28:13 5:25:4", __LINE__);
}

CPPL_TEST(the_words_used_as_ordinary_cpp_names_are_not_tokens) {
    // Whatever the flag says, a name the recognizer left to C++ is never a
    // C++L token.
    CPPL_CHECK(cppl_data("int exact = 1;\n"
                         "struct contradiction {};\n"
                         "int refl(int apply) { contradiction verdict; return apply + exact; }\n"
                         "int law = 2; int proof(int);\n"
                         "// exact h; contradiction h;\n"
                         "const char* cases = \"refl;\";\n",
                         true)
                   .empty());
}

CPPL_TEST(a_runtime_claim_is_a_token_only_when_the_compile_recognized_claims) {
    const std::string text = "proof nothing()\n"
                             "    proves (true)\n"
                             "{\n"
                             "    refl;\n"
                             "}\n"
                             "\n"
                             "verified int f(int x)\n"
                             "    ensures (result == x)\n"
                             "{\n"
                             "    if (x != x) {\n"
                             "        contradiction nothing;\n"
                             "    }\n"
                             "    return x;\n"
                             "}\n";
    expect(keywords_of(text, false), "0:0:5 1:4:6 3:4:4 6:0:8 7:4:7", __LINE__);
    expect(keywords_of(text, true), "0:0:5 1:4:6 3:4:4 6:0:8 7:4:7 10:8:13", __LINE__);
}

// SPEC: WORD-012
// A split in a verified body, and the claims its arms hold, are tokens only when
// the compile recognized splits. A claim in an arm is the split's: it is one
// token under that flag, never a second one under the claims flag.
CPPL_TEST(a_split_on_a_path_is_tokens_only_when_the_compile_recognized_splits) {
    const std::string text = "proof nothing()\n"
                             "    proves (true)\n"
                             "{\n"
                             "    refl;\n"
                             "}\n"
                             "\n"
                             "verified int f(State s)\n"
                             "    ensures (result == 0)\n"
                             "{\n"
                             "    cases s {\n"
                             "        State::idle => {\n"
                             "            contradiction nothing;\n"
                             "        }\n"
                             "        omit State::busy by contradiction nothing;\n"
                             "        unnamed(v) => {}\n"
                             "    }\n"
                             "    return 0;\n"
                             "}\n";
    const std::string declarations = "0:0:5 1:4:6 3:4:4 6:0:8 7:4:7";
    expect(keywords_of(text, false, false), declarations, __LINE__);
    expect(keywords_of(text, true, false), declarations, __LINE__);
    expect(keywords_of(text, true, true), declarations + " 9:4:5 11:12:13 13:8:4 13:25:2 13:28:13", __LINE__);
}

CPPL_TEST(a_position_counts_utf16_code_units_not_bytes) {
    // U+2200 is three UTF-8 bytes and one UTF-16 unit; U+1F600 is four bytes
    // and two units, a surrogate pair.
    expect(keywords_of("proof p()\n"
                       "    proves (true)\n"
                       "{\n"
                       "    /* \xE2\x88\x80 \xF0\x9F\x98\x80 */ refl;\n"
                       "}\n",
                       false),
           "0:0:5 1:4:6 3:15:4", __LINE__);
}

CPPL_TEST(tabs_and_runs_of_spaces_keep_the_columns_they_were_written_at) {
    // The preprocessor collapses such runs, which is why these positions come
    // from the buffer as written and never from the preprocessed unit.
    const std::string text = "proof p(int a)\n"
                             "    proves (a == a)\n"
                             "{\n"
                             "\t\tassume h : a == a;   exact   h;\n"
                             "}\n";
    expect(keywords_of(text, false), "0:0:5 1:4:6 3:2:6 3:23:5", __LINE__);
}

CPPL_TEST(overlapping_tokens_keep_the_first_and_positions_are_relative) {
    const std::string text = "ab cd\nef\n";
    // Two tokens on one line: the second's character is relative to the first;
    // the next line's is absolute. A token overlapping one before it is dropped.
    CPPL_CHECK(encode({SemanticToken{{0, 2}, TokenType::Keyword, 0}, SemanticToken{{3, 2}, TokenType::Variable, 1},
                       SemanticToken{{1, 3}, TokenType::Function, 0}, SemanticToken{{6, 2}, TokenType::Macro, 0}},
                      text) == (std::vector<std::uint32_t>{0, 0, 2, static_cast<std::uint32_t>(TokenType::Keyword), 0,
                                                           0, 3, 2, static_cast<std::uint32_t>(TokenType::Variable), 1,
                                                           1, 0, 2, static_cast<std::uint32_t>(TokenType::Macro), 0}));
}

CPPL_TEST(each_cpp_name_is_the_kind_of_thing_clang_says_it_names) {
    Server server = fixture_server();
    // A macro's parameters are not the macro; only a class's members are static.
    open(server, "file:///names.cpp",
         "namespace geo {\n"
         "struct Point { int x; static int count; };\n"
         "const int origin = 0;\n"
         "static int norm(const Point& p) { return p.x + origin; }\n"
         "#define TWICE(v) ((v) * 2)\n"
         "enum class Side { left };\n"
         "int twice_norm(Point p) { return TWICE(norm(p)) + static_cast<int>(Side::left) + Point::count; }\n"
         "}\n");
    expect(described(server_data(server, "file:///names.cpp")),
           "0:10:3:namespace+declaration "
           "1:7:5:struct+declaration 1:19:1:property+declaration 1:33:5:variable+declaration+static "
           "2:10:6:variable+declaration+readonly "
           "3:11:4:function+declaration 3:22:5:struct 3:29:1:parameter+declaration+readonly "
           "3:41:1:parameter+readonly 3:43:1:property 3:47:6:variable+readonly "
           "4:8:5:macro+declaration "
           "5:11:4:enum+declaration 5:18:4:enumMember+declaration+readonly "
           "6:4:10:function+declaration 6:15:5:struct 6:21:1:parameter+declaration 6:33:5:macro "
           "6:39:4:function 6:44:1:parameter 6:67:4:enum 6:73:4:enumMember+readonly 6:81:5:struct "
           "6:88:5:variable+static",
           __LINE__);
}

CPPL_TEST(a_laws_names_are_tokens_where_the_law_writes_them) {
    // The Law's name is the recognizer's; the names inside its proposition are
    // Clang's, read where the projection copied them; the proof's evidence is
    // what the compile resolved it to.
    Server server = fixture_server();
    open(server, "file:///law.cpp",
         "law holds(int x)\n"
         "    proves (x == x)\n"
         "{\n"
         "    refl;\n"
         "}\n"
         "proof again(int y)\n"
         "    proves (holds(y))\n"
         "{\n"
         "    exact holds(y);\n"
         "}\n");
    expect(described(server_data(server, "file:///law.cpp")),
           "0:0:3:keyword 0:4:5:function+declaration 0:14:1:parameter+declaration 1:4:6:keyword 1:12:1:parameter "
           "1:17:1:parameter 3:4:4:keyword 5:0:5:keyword 5:6:5:function+declaration 5:16:1:parameter+declaration "
           "6:4:6:keyword 6:12:5:function 6:18:1:parameter 8:4:5:keyword 8:10:5:function 8:16:1:parameter",
           __LINE__);
}

CPPL_TEST(the_server_colors_every_runtime_claim_the_compiler_recognized) {
    Server server = fixture_server();
    const std::string text = read_fixture("impossible_path.cpp");
    open(server, "file:///impossible_path.cpp", text);
    // Two `refl;` in its proofs and six claims in its verified bodies.
    const std::vector<std::uint32_t> data = server_data(server, "file:///impossible_path.cpp");
    CPPL_CHECK_EQ(keywords_spelling(text, data, {"refl"}), std::size_t{2});
    CPPL_CHECK_EQ(keywords_spelling(text, data, {"contradiction"}), std::size_t{6});
}

CPPL_TEST(a_claim_the_translation_unit_reads_as_cpp_is_not_colored) {
    // Read alone, the file holds a claim; its header declares `contradiction`,
    // so the unit reads the same statement as a declaration.
    const std::string text = read_fixture("contradiction_named_by_a_header.cpp");
    {
        const frontend::TokenStream stream = frontend::lex(text, "main.cpp");
        diagnostics::Engine engine;
        CPPL_CHECK_EQ(frontend::recognize(stream, engine).path_contradictions.size(), std::size_t{1});
    }

    Server server = fixture_server();
    std::vector<Diagnostic> published;
    server.set_diagnostic_publisher(
        [&published](const std::string&, std::vector<Diagnostic> diagnostics) { published = std::move(diagnostics); });
    open(server, "file:///contradiction_named_by_a_header.cpp", text);

    // The compile saw the header, which is what this test depends on.
    CPPL_CHECK(std::ranges::any_of(published, [](const Diagnostic& diagnostic) {
        return diagnostic.message.find("'contradiction' is also a name") != std::string::npos;
    }));
    // Only the proof's `refl;` is a proof statement.
    const std::vector<std::uint32_t> data = server_data(server, "file:///contradiction_named_by_a_header.cpp");
    CPPL_CHECK_EQ(keywords_spelling(text, data, {"refl"}), std::size_t{1});
    CPPL_CHECK_EQ(keywords_spelling(text, data, {"contradiction"}), std::size_t{0});
}

CPPL_TEST(an_edit_that_makes_the_claim_ordinary_cpp_uncolors_it) {
    // The recognition of claims is refreshed by every compile, never kept from
    // an earlier version of the buffer.
    Server server = fixture_server();
    const std::string before = read_fixture("impossible_path.cpp");
    open(server, "file:///edited.cpp", before);
    CPPL_CHECK_EQ(keywords_spelling(before, server_data(server, "file:///edited.cpp"), {"contradiction"}),
                  std::size_t{6});

    VersionedTextDocumentIdentifier id;
    id.uri = "file:///edited.cpp";
    id.version = 2;
    TextDocumentContentChangeEvent change;
    change.text = read_fixture("contradiction_named_by_a_header.cpp");
    server.text_document_did_change(id, {change});
    CPPL_CHECK_EQ(keywords_spelling(change.text, server_data(server, "file:///edited.cpp"), {"contradiction"}),
                  std::size_t{0});
}

CPPL_TEST(without_a_compile_no_runtime_claim_is_colored) {
    // No Clang to compile with, so no unit to confirm the claims against: the
    // proofs' own keywords are still certain, the claims are not.
    Server server = fixture_server("/nonexistent/cppl-test/clang");
    const std::string text = read_fixture("impossible_path.cpp");
    open(server, "file:///impossible_path.cpp", text);
    const std::vector<std::uint32_t> data = server_data(server, "file:///impossible_path.cpp");
    CPPL_CHECK_EQ(keywords_spelling(text, data, {"refl"}), std::size_t{2});
    CPPL_CHECK_EQ(keywords_spelling(text, data, {"contradiction"}), std::size_t{0});
}

CPPL_TEST(semantic_tokens_of_an_unknown_document_are_null) {
    Server server;
    TextDocumentIdentifier id;
    id.uri = "file:///never-opened.cpp";
    CPPL_CHECK(!server.text_document_semantic_tokens(id).has_value());
}
