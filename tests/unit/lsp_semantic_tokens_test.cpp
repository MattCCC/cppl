// Semantic tokens for the proof-statement keywords a spelling-based grammar
// cannot color (src/lsp/include/cppl/lsp/semantic_tokens.hpp).

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

// Each token as `line:character:length`, in absolute coordinates, after
// checking the parts of the encoding every token here shares.
std::string decoded(const std::vector<std::uint32_t>& data) {
    CPPL_CHECK_EQ(data.size() % 5, std::size_t{0});
    std::string out;
    std::uint32_t line = 0;
    std::uint32_t character = 0;
    for (std::size_t at = 0; at + 5 <= data.size(); at += 5) {
        line += data[at];
        character = data[at] == 0 ? character + data[at + 1] : data[at + 1];
        CPPL_CHECK_EQ(data[at + 3], 0u); // the legend's one type, `keyword`
        CPPL_CHECK_EQ(data[at + 4], 0u);
        out += (out.empty() ? "" : " ") + std::to_string(line) + ":" + std::to_string(character) + ":" +
               std::to_string(data[at + 2]);
    }
    return out;
}

std::string tokens_of(const std::string& text, bool path_claims_recognized) {
    const frontend::TokenStream stream = frontend::lex(text, "main.cpp");
    diagnostics::Engine engine;
    const frontend::Syntax syntax = frontend::recognize(stream, engine);
    return decoded(proof_keyword_tokens(syntax, text, path_claims_recognized));
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

std::string server_tokens(const Server& server, const std::string& uri) {
    TextDocumentIdentifier id;
    id.uri = uri;
    const std::optional<std::vector<std::uint32_t>> data = server.text_document_semantic_tokens(id);
    CPPL_CHECK(data.has_value());
    return data.has_value() ? decoded(*data) : std::string{};
}

// Whether every token spells one of `words` in `text`.
bool spells_only(const std::string& text, const std::vector<std::uint32_t>& data,
                 const std::vector<std::string>& words) {
    std::vector<std::size_t> line_starts = {0};
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (text[at] == '\n') {
            line_starts.push_back(at + 1);
        }
    }
    std::uint32_t line = 0;
    std::uint32_t character = 0;
    for (std::size_t at = 0; at + 5 <= data.size(); at += 5) {
        line += data[at];
        character = data[at] == 0 ? character + data[at + 1] : data[at + 1];
        // Every line these fixtures color is ASCII, so a character is a byte.
        const std::string word = text.substr(line_starts[line] + character, data[at + 2]);
        if (std::ranges::find(words, word) == words.end()) {
            return false;
        }
    }
    return true;
}

std::size_t count(const std::string& decoded_tokens) {
    return decoded_tokens.empty() ? 0 : static_cast<std::size_t>(std::ranges::count(decoded_tokens, ' ')) + 1;
}

} // namespace

CPPL_TEST(every_proof_statement_keyword_is_a_token) {
    CPPL_CHECK_EQ(tokens_of("proof p(int a)\n"
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
                            false),
                  std::string("3:4:6 4:4:7 5:4:5 6:4:5 7:4:13 8:4:9 9:4:4"));
}

CPPL_TEST(statements_inside_arms_and_omissions_are_tokens_and_omit_by_are_left_to_the_grammar) {
    // `omit` and `by` are colored by the TextMate grammar, which can see the
    // whole omission; `cases` and the statements it holds are not its to color.
    CPPL_CHECK_EQ(tokens_of("proof q(State s)\n"
                            "    proves (true)\n"
                            "{\n"
                            "    cases s {\n"
                            "        omit State::idle by contradiction never;\n"
                            "        State::busy => { refl; }\n"
                            "    }\n"
                            "}\n",
                            false),
                  std::string("3:4:5 4:28:13 5:25:4"));
}

CPPL_TEST(the_words_used_as_ordinary_cpp_names_are_not_tokens) {
    // Whatever the flag says, a name the recognizer left to C++ is never colored.
    CPPL_CHECK(tokens_of("int exact = 1;\n"
                         "struct contradiction {};\n"
                         "int refl(int apply) { contradiction verdict; return apply + exact; }\n"
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
    CPPL_CHECK_EQ(tokens_of(text, false), std::string("3:4:4"));
    CPPL_CHECK_EQ(tokens_of(text, true), std::string("3:4:4 10:8:13"));
}

CPPL_TEST(a_position_counts_utf16_code_units_not_bytes) {
    // U+2200 is three UTF-8 bytes and one UTF-16 unit; U+1F600 is four bytes
    // and two units, a surrogate pair.
    CPPL_CHECK_EQ(tokens_of("proof p()\n"
                            "    proves (true)\n"
                            "{\n"
                            "    /* \xE2\x88\x80 \xF0\x9F\x98\x80 */ refl;\n"
                            "}\n",
                            false),
                  std::string("3:15:4"));
}

CPPL_TEST(tabs_and_runs_of_spaces_keep_the_columns_they_were_written_at) {
    // The preprocessor collapses such runs, which is why these positions come
    // from the buffer as written and never from the preprocessed unit.
    const std::string text = "proof p(int a)\n"
                             "    proves (a == a)\n"
                             "{\n"
                             "\t\tassume h : a == a;   exact   h;\n"
                             "}\n";
    CPPL_CHECK_EQ(tokens_of(text, false), std::string("3:2:6 3:23:5"));

    const frontend::TokenStream stream = frontend::lex(text, "main.cpp");
    diagnostics::Engine engine;
    const frontend::Syntax syntax = frontend::recognize(stream, engine);
    // Two tokens on one line: the second's character is relative to the first.
    CPPL_CHECK(proof_keyword_tokens(syntax, text, false) ==
               (std::vector<std::uint32_t>{3, 2, 6, 0, 0, 0, 21, 5, 0, 0}));
}

CPPL_TEST(a_document_without_proof_statements_has_no_tokens) {
    CPPL_CHECK(tokens_of("", true).empty());
    CPPL_CHECK(tokens_of("int main() { return 0; }\n", true).empty());
}

CPPL_TEST(the_server_colors_every_runtime_claim_the_compiler_recognized) {
    Server server = fixture_server();
    const std::string text = read_fixture("impossible_path.cpp");
    open(server, "file:///impossible_path.cpp", text);

    TextDocumentIdentifier id;
    id.uri = "file:///impossible_path.cpp";
    const std::optional<std::vector<std::uint32_t>> data = server.text_document_semantic_tokens(id);
    CPPL_CHECK(data.has_value());
    // Two `refl;` in its proofs and six claims in its verified bodies.
    CPPL_CHECK_EQ(count(decoded(*data)), std::size_t{8});
    CPPL_CHECK(spells_only(text, *data, {"refl", "contradiction"}));
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
    // Only the proof's `refl;` is colored.
    CPPL_CHECK_EQ(server_tokens(server, "file:///contradiction_named_by_a_header.cpp"), std::string("15:4:4"));
}

CPPL_TEST(an_edit_that_makes_the_claim_ordinary_cpp_uncolors_it) {
    // The recognition of claims is refreshed by every compile, never kept from
    // an earlier version of the buffer.
    Server server = fixture_server();
    open(server, "file:///edited.cpp", read_fixture("impossible_path.cpp"));
    CPPL_CHECK_EQ(count(server_tokens(server, "file:///edited.cpp")), std::size_t{8});

    VersionedTextDocumentIdentifier id;
    id.uri = "file:///edited.cpp";
    id.version = 2;
    TextDocumentContentChangeEvent change;
    change.text = read_fixture("contradiction_named_by_a_header.cpp");
    server.text_document_did_change(id, {change});
    CPPL_CHECK_EQ(server_tokens(server, "file:///edited.cpp"), std::string("15:4:4"));
}

CPPL_TEST(without_a_compile_no_runtime_claim_is_colored) {
    // No Clang, so no unit to confirm the claims against: the proofs' own
    // keywords are still certain, the claims are not.
    Server server = fixture_server("/nonexistent/cppl-test/clang");
    const std::string text = read_fixture("impossible_path.cpp");
    open(server, "file:///impossible_path.cpp", text);

    TextDocumentIdentifier id;
    id.uri = "file:///impossible_path.cpp";
    const std::optional<std::vector<std::uint32_t>> data = server.text_document_semantic_tokens(id);
    CPPL_CHECK(data.has_value());
    CPPL_CHECK_EQ(count(decoded(*data)), std::size_t{2});
    CPPL_CHECK(spells_only(text, *data, {"refl"}));
}

CPPL_TEST(semantic_tokens_of_an_unknown_document_are_null) {
    const Server server;
    TextDocumentIdentifier id;
    id.uri = "file:///never-opened.cpp";
    CPPL_CHECK(!server.text_document_semantic_tokens(id).has_value());
}
