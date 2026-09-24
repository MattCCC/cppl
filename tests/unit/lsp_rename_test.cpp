// Rename: every place references finds, rewritten, and only where that is
// safe -- each place still spells the name, lies in a file the editor holds
// open or the workspace index reads, and C++L reads the same afterwards
// (src/lsp/include/cppl/lsp/rename.hpp, tools/cppl-lsp/README.md, "Rename").

#include "cppl/driver/scratch.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/rename.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/lsp/uri.hpp"
#include "cppl/testing/test.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace cppl::lsp;

namespace {

constexpr std::chrono::seconds kIndexed(300);

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string uri_of(const std::filesystem::path& path) {
    return path_to_uri(path.string());
}

void open(Server& server, const std::string& uri, const std::string& text) {
    TextDocumentItem item;
    item.uri = uri;
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);
}

Position position_of(const std::string& text, const std::string& needle, std::size_t into = 0) {
    const std::size_t at = text.find(needle);
    if (at == std::string::npos) {
        ::cppl::testing::fail(__FILE__, __LINE__, "no '" + needle + "' in the text");
    }
    return PositionMapper(text).byte_offset_to_position(at + into);
}

TextEdit edit_at(const std::string& text, const std::string& needle, std::size_t length, std::string replacement) {
    const Position start = position_of(text, needle);
    return TextEdit{Range{start, Position{start.line, start.character + static_cast<std::uint32_t>(length)}},
                    std::move(replacement)};
}

std::expected<WorkspaceEdit, std::string> rename(Server& server, const std::string& uri, const Position& position,
                                                 const std::string& name) {
    TextDocumentIdentifier id;
    id.uri = uri;
    return server.text_document_rename(id, position, name);
}

std::expected<PrepareRename, std::string> prepare(Server& server, const std::string& uri, const Position& position) {
    TextDocumentIdentifier id;
    id.uri = uri;
    return server.text_document_prepare_rename(id, position);
}

// Each renamed document's text as the edit leaves it, by the file's name.
std::map<std::string, std::string> renamed(const WorkspaceEdit& edit, const std::map<std::string, std::string>& texts) {
    std::map<std::string, std::string> out;
    for (const auto& [uri, edits] : edit.changes) {
        const std::string file = uri.substr(uri.rfind('/') + 1);
        const auto text = texts.find(file);
        if (text == texts.end()) {
            ::cppl::testing::fail(__FILE__, __LINE__, "an edit to an unexpected file, " + uri);
            continue;
        }
        out.emplace(file, apply_edits(text->second, edits));
    }
    return out;
}

// The reason a rename was refused, or "renamed" when it was not.
template <typename T> std::string refusal(const std::expected<T, std::string>& result) {
    return result.has_value() ? std::string("renamed") : result.error();
}

} // namespace

CPPL_TEST(a_new_name_is_a_cpp_identifier_that_is_no_keyword) {
    CPPL_CHECK(!refuse_identifier("total").has_value());
    CPPL_CHECK(!refuse_identifier("_Total2").has_value());
    // A C++L word is an identifier; where it may be written is changes_cppl's.
    CPPL_CHECK(!refuse_identifier("law").has_value());
    CPPL_CHECK_EQ(refuse_identifier("").value_or(""), std::string("a name cannot be empty"));
    CPPL_CHECK_EQ(refuse_identifier("2nd").value_or(""), std::string("'2nd' is not a C++ identifier"));
    CPPL_CHECK_EQ(refuse_identifier("a-b").value_or(""), std::string("'a-b' is not a C++ identifier"));
    CPPL_CHECK_EQ(refuse_identifier("a b").value_or(""), std::string("'a b' is not a C++ identifier"));
    CPPL_CHECK_EQ(refuse_identifier("int").value_or(""), std::string("'int' is a C++ keyword"));
    CPPL_CHECK_EQ(refuse_identifier("co_await").value_or(""), std::string("'co_await' is a C++ keyword"));
    CPPL_CHECK_EQ(refuse_identifier("xor_eq").value_or(""), std::string("'xor_eq' is a C++ keyword"));
}

CPPL_TEST(edits_apply_last_first_counted_in_utf16_units) {
    // `é` is two bytes and one UTF-16 unit; `😀` four bytes and two units.
    const std::string text = "int é = 1; int 😀x = é + é;\n";
    const std::vector<TextEdit> edits = {
        TextEdit{Range{{0, 4}, {0, 5}}, "e_acute"},
        TextEdit{Range{{0, 25}, {0, 26}}, "e_acute"},
        TextEdit{Range{{0, 21}, {0, 22}}, "e_acute"},
    };
    CPPL_CHECK_EQ(apply_edits(text, edits), std::string("int e_acute = 1; int 😀x = e_acute + e_acute;\n"));
    CPPL_CHECK_EQ(apply_edits(text, {}), text);
}

CPPL_TEST(a_cppl_word_is_written_only_outside_cppl) {
    const std::string text = "int limit();\n"
                             "int use() { return limit(); }\n"
                             "law bounded(unsigned x)\n"
                             "    proves (limit() == limit());\n";
    // Outside every C++L construct `law` is an ordinary identifier.
    CPPL_CHECK(!changes_cppl("/work/a.cpp", text,
                             {edit_at(text, "limit();\nint", 5, "law"), edit_at(text, "limit(); }", 5, "law")}, "law")
                    .has_value());
    // Inside a Law's proposition, `result` means something else.
    const std::optional<std::string> inside =
        changes_cppl("/work/a.cpp", text,
                     {edit_at(text, "limit() ==", 5, "result"), edit_at(text, "limit());", 5, "result")}, "result");
    CPPL_CHECK_EQ(inside.value_or(""),
                  std::string("'result' is a C++L word, and a place to rename in /work/a.cpp lies inside C++L"));
    // Anywhere else, an ordinary name is written as any other.
    CPPL_CHECK(!changes_cppl("/work/a.cpp", text,
                             {edit_at(text, "limit() ==", 5, "bound"), edit_at(text, "limit());", 5, "bound")}, "bound")
                    .has_value());
}

CPPL_TEST(a_rename_that_changes_what_cppl_reads_is_refused) {
    // An unqualified arm label is read only when it is a state a
    // representation reserves (WORD-005): renaming an enumerator to one turns
    // a label the recognizer refused into one it reads as that state.
    const std::string text = "enum Light { red };\n"
                             "proof only(Light l)\n"
                             "    proves (l == l)\n"
                             "{\n"
                             "    cases l {\n"
                             "        red => { refl; }\n"
                             "    }\n"
                             "}\n";
    CPPL_CHECK_EQ(changes_cppl("/work/light.cpp", text,
                               {edit_at(text, "red }", 3, "none"), edit_at(text, "red =>", 3, "none")}, "none")
                      .value_or(""),
                  std::string("renaming to 'none' would change the C++L /work/light.cpp holds"));
    CPPL_CHECK(!changes_cppl("/work/light.cpp", text,
                             {edit_at(text, "red }", 3, "crimson"), edit_at(text, "red =>", 3, "crimson")}, "crimson")
                    .has_value());

    // Nested in another split's arm, the proof holds as many statements
    // either way, and the arm holds another.
    const std::string nested = "enum class Mode : unsigned { idle = 0u, busy = 1u };\n"
                               "enum Light { red };\n"
                               "proof both(Mode m, Light l)\n"
                               "    proves (l == l)\n"
                               "{\n"
                               "    cases m {\n"
                               "        Mode::idle => {\n"
                               "            cases l {\n"
                               "                red => { refl; }\n"
                               "            }\n"
                               "        }\n"
                               "        Mode::busy => { refl; }\n"
                               "    }\n"
                               "}\n";
    CPPL_CHECK_EQ(changes_cppl("/work/nested.cpp", nested,
                               {edit_at(nested, "red }", 3, "none"), edit_at(nested, "red =>", 3, "none")}, "none")
                      .value_or(""),
                  std::string("renaming to 'none' would change the C++L /work/nested.cpp holds"));
}

CPPL_TEST(a_rename_that_changes_a_case_split_on_a_path_is_refused) {
    // The same, for a split in a verified body, nested in another's arm.
    const std::string text = "enum class Mode : unsigned { idle = 0u, busy = 1u };\n"
                             "enum Light { red };\n"
                             "verified unsigned settled(Mode m, Light l)\n"
                             "    ensures (result == 0u)\n"
                             "{\n"
                             "    cases m {\n"
                             "        Mode::idle => {\n"
                             "            cases l {\n"
                             "                red => {\n"
                             "                }\n"
                             "            }\n"
                             "        }\n"
                             "        Mode::busy => {\n"
                             "        }\n"
                             "    }\n"
                             "    return 0u;\n"
                             "}\n";
    CPPL_CHECK_EQ(changes_cppl("/work/split.cpp", text,
                               {edit_at(text, "red }", 3, "none"), edit_at(text, "red =>", 3, "none")}, "none")
                      .value_or(""),
                  std::string("renaming to 'none' would change the C++L /work/split.cpp holds"));
    CPPL_CHECK(!changes_cppl("/work/split.cpp", text,
                             {edit_at(text, "red }", 3, "crimson"), edit_at(text, "red =>", 3, "crimson")}, "crimson")
                    .has_value());
}

CPPL_TEST(a_local_is_renamed_everywhere_it_is_written_and_nowhere_else) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    const std::string text = "int count = 7;\n"
                             "int main() {\n"
                             "    int count = 0;\n"
                             "    count += ::count;\n"
                             "    return count;\n"
                             "}\n";
    open(server, "file:///work/main.cpp", text);
    const auto edit = rename(server, "file:///work/main.cpp", position_of(text, "count += ", 2), "total");
    CPPL_CHECK_EQ(refusal(edit), std::string("renamed"));
    if (edit.has_value()) {
        CPPL_CHECK_EQ(renamed(*edit, {{"main.cpp", text}}).at("main.cpp"), std::string("int count = 7;\n"
                                                                                       "int main() {\n"
                                                                                       "    int total = 0;\n"
                                                                                       "    total += ::count;\n"
                                                                                       "    return total;\n"
                                                                                       "}\n"));
    }
    // The same name again renames nothing.
    const auto same = rename(server, "file:///work/main.cpp", position_of(text, "count += "), "count");
    CPPL_CHECK(same.has_value() && same->changes.empty());
}

CPPL_TEST(a_rename_reaches_every_file_of_the_workspace_as_the_editor_holds_it) {
    const cppl::driver::ScratchDirectory scratch;
    const std::filesystem::path root = scratch.path() / "project";
    const std::string header = "#pragma once\nint shared_value();\n";
    const std::string first = "#include \"shared.hpp\"\nint first() { return shared_value(); }\n";
    const std::string second = "#include \"shared.hpp\"\nint second() { return shared_value() + 1; }\n";
    write(root / "shared.hpp", header);
    write(root / "first.cpp", "stale text on disk, never read\n");
    write(root / "second.cpp", second);

    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    server.initialize({}, {root.string()});
    CPPL_CHECK(server.wait_for_index(kIndexed));
    open(server, uri_of(root / "first.cpp"), first);
    const auto edit = rename(server, uri_of(root / "first.cpp"), position_of(first, "shared_value"), "common_value");
    CPPL_CHECK_EQ(refusal(edit), std::string("renamed"));
    if (edit.has_value()) {
        const std::map<std::string, std::string> after =
            renamed(*edit, {{"shared.hpp", header}, {"first.cpp", first}, {"second.cpp", second}});
        CPPL_CHECK_EQ(after.size(), std::size_t{3});
        CPPL_CHECK_EQ(after.at("shared.hpp"), std::string("#pragma once\nint common_value();\n"));
        CPPL_CHECK_EQ(after.at("first.cpp"),
                      std::string("#include \"shared.hpp\"\nint first() { return common_value(); }\n"));
        CPPL_CHECK_EQ(after.at("second.cpp"),
                      std::string("#include \"shared.hpp\"\nint second() { return common_value() + 1; }\n"));
    }
}

CPPL_TEST(a_place_that_no_longer_spells_the_name_refuses_the_rename) {
    const cppl::driver::ScratchDirectory scratch;
    const std::filesystem::path root = scratch.path() / "project";
    const std::string first = "int shared_value();\nint first() { return shared_value(); }\n";
    write(root / "first.cpp", first);
    write(root / "second.cpp", "int shared_value();\nint second() { return shared_value(); }\n");

    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    server.initialize({}, {root.string()});
    CPPL_CHECK(server.wait_for_index(kIndexed));
    // Changed on disk with its time kept, so that the index still holds where
    // the name was.
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(root / "second.cpp");
    write(root / "second.cpp", "int shared_value();\nint second() { return 1 + shared_value(); }\n");
    std::filesystem::last_write_time(root / "second.cpp", written);
    open(server, uri_of(root / "first.cpp"), first);
    const std::string refused =
        refusal(rename(server, uri_of(root / "first.cpp"), position_of(first, "shared_value();\nint"), "common"));
    CPPL_CHECK(refused.starts_with("'shared_value' is not written as such at "));
    CPPL_CHECK(refused.ends_with("second.cpp:2:23"));
}

CPPL_TEST(the_place_to_rename_is_the_one_under_the_cursor_in_this_document) {
    // The header declares the name where, on the same line, this document
    // writes it too, but further left.
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20", "-I/work"});
    const std::string header = "#pragma once\nint /* 12345 */shared_value();\n";
    const std::string text = "#include \"h.hpp\"\nint f() { return shared_value(); }\n";
    open(server, "file:///work/h.hpp", header);
    open(server, "file:///work/main.cpp", text);
    const auto here = prepare(server, "file:///work/main.cpp", position_of(text, "shared_value", 3));
    CPPL_CHECK_EQ(refusal(here), std::string("renamed"));
    if (here.has_value()) {
        CPPL_CHECK_EQ(here->placeholder, std::string("shared_value"));
        CPPL_CHECK_EQ(here->range.start.character, position_of(text, "shared_value").character);
    }
}

CPPL_TEST(a_name_only_generated_text_declares_is_not_renamed) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    const std::string text = "verified int zero()\n"
                             "    ensures (result == 0)\n"
                             "{\n"
                             "    return 0;\n"
                             "}\n";
    open(server, "file:///work/zero.cpp", text);
    CPPL_CHECK_EQ(refusal(prepare(server, "file:///work/zero.cpp", position_of(text, "result"))),
                  std::string("'result' is declared only in text C++L generated"));
}

CPPL_TEST(a_name_declared_outside_the_workspace_is_not_renamed) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    const std::string text = "#include <cstdlib>\nint main() { return std::abs(-1); }\n";
    open(server, "file:///work/main.cpp", text);
    const std::string refused = refusal(rename(server, "file:///work/main.cpp", position_of(text, "abs"), "magnitude"));
    CPPL_CHECK(refused.starts_with("'abs' is also written in "));
    CPPL_CHECK(refused.ends_with(", which is neither open nor in the workspace"));
    CPPL_CHECK_EQ(refusal(prepare(server, "file:///work/main.cpp", position_of(text, "abs"))), refused);
}

CPPL_TEST(a_keyword_or_a_place_with_no_name_is_not_renamed) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    const std::string text = "int value = 1;\nint main() { return value; }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(refusal(rename(server, "file:///work/main.cpp", position_of(text, "value;"), "int")),
                  std::string("'int' is a C++ keyword"));
    CPPL_CHECK_EQ(refusal(prepare(server, "file:///work/main.cpp", position_of(text, "return"))),
                  std::string("there is no name here to rename"));
    CPPL_CHECK_EQ(refusal(prepare(server, "file:///work/never-opened.cpp", Position{0, 0})),
                  std::string("the document is not open"));
    // Where a name is, the name itself is what the editor offers to rewrite.
    const auto here = prepare(server, "file:///work/main.cpp", position_of(text, "value;", 3));
    CPPL_CHECK(here.has_value());
    if (here.has_value()) {
        CPPL_CHECK_EQ(here->placeholder, std::string("value"));
        CPPL_CHECK_EQ(here->range.start.line, std::uint32_t{1});
        CPPL_CHECK_EQ(here->range.start.character, position_of(text, "value;").character);
        CPPL_CHECK_EQ(here->range.end.character, position_of(text, "value;", 5).character);
    }
}

CPPL_TEST(a_class_is_renamed_with_its_constructors_and_destructor) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    const std::string text = "struct Box {\n"
                             "    Box();\n"
                             "    explicit Box(int v);\n"
                             "    ~Box();\n"
                             "    int v = 0;\n"
                             "};\n"
                             "Box::Box() = default;\n"
                             "Box::~Box() = default;\n"
                             "Box make() { return Box(1); }\n";
    open(server, "file:///work/box.cpp", text);
    const auto edit = rename(server, "file:///work/box.cpp", position_of(text, "Box make"), "Crate");
    CPPL_CHECK_EQ(refusal(edit), std::string("renamed"));
    if (edit.has_value()) {
        CPPL_CHECK_EQ(renamed(*edit, {{"box.cpp", text}}).at("box.cpp"),
                      std::string("struct Crate {\n"
                                  "    Crate();\n"
                                  "    explicit Crate(int v);\n"
                                  "    ~Crate();\n"
                                  "    int v = 0;\n"
                                  "};\n"
                                  "Crate::Crate() = default;\n"
                                  "Crate::~Crate() = default;\n"
                                  "Crate make() { return Crate(1); }\n"));
    }
}

CPPL_TEST(a_class_is_renamed_from_one_of_its_constructors) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    const std::string text = "struct Box {\n"
                             "    explicit Box(int v) : v(v) {}\n"
                             "    int v;\n"
                             "};\n"
                             "Box make() { return Box(1); }\n";
    open(server, "file:///work/box.cpp", text);
    const auto edit = rename(server, "file:///work/box.cpp", position_of(text, "Box(int"), "Crate");
    CPPL_CHECK_EQ(refusal(edit), std::string("renamed"));
    if (edit.has_value()) {
        CPPL_CHECK_EQ(renamed(*edit, {{"box.cpp", text}}).at("box.cpp"),
                      std::string("struct Crate {\n"
                                  "    explicit Crate(int v) : v(v) {}\n"
                                  "    int v;\n"
                                  "};\n"
                                  "Crate make() { return Crate(1); }\n"));
    }
}

CPPL_TEST(a_name_a_macro_body_spells_is_not_renamed_but_a_macro_argument_is) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    const std::string text = "int value();\n"
                             "int other();\n"
                             "#define TWICE(x) ((x) * 2)\n"
                             "#define VALUE value()\n"
                             "int first() { return TWICE(value()); }\n"
                             "int second() { return VALUE + TWICE(other()); }\n";
    open(server, "file:///work/macros.cpp", text);
    // `VALUE`'s body spells `value`: renaming it there would rename it for
    // every expansion, and leaving it would break this one.
    CPPL_CHECK_EQ(refusal(rename(server, "file:///work/macros.cpp", position_of(text, "value();"), "amount")),
                  std::string("'value' is used through a macro at /work/macros.cpp:6:23, which spells it in its body"));
    // An argument is written where the macro is used, and is renamed there.
    const auto edit = rename(server, "file:///work/macros.cpp", position_of(text, "other();"), "another");
    CPPL_CHECK_EQ(refusal(edit), std::string("renamed"));
    if (edit.has_value()) {
        const std::string after = renamed(*edit, {{"macros.cpp", text}}).at("macros.cpp");
        CPPL_CHECK(after.find("int another();") != std::string::npos);
        CPPL_CHECK(after.find("TWICE(another())") != std::string::npos);
    }
}

CPPL_TEST(a_macro_in_a_closed_file_that_spells_the_name_refuses_the_rename) {
    const cppl::driver::ScratchDirectory scratch;
    const std::filesystem::path root = scratch.path() / "project";
    const std::string header = "#pragma once\nint shared_value();\n";
    const std::string first = "#include \"shared.hpp\"\nint first() { return shared_value(); }\n";
    write(root / "shared.hpp", header);
    write(root / "first.cpp", first);
    write(root / "hidden.cpp", "#include \"shared.hpp\"\n#define GET shared_value()\nint hidden() { return GET; }\n");

    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    server.initialize({}, {root.string()});
    CPPL_CHECK(server.wait_for_index(kIndexed));
    open(server, uri_of(root / "first.cpp"), first);
    const std::string refused =
        refusal(rename(server, uri_of(root / "first.cpp"), position_of(first, "shared_value"), "common_value"));
    CPPL_CHECK(refused.starts_with("'shared_value' is used through a macro at "));
    CPPL_CHECK(refused.ends_with("hidden.cpp:3:23, which spells it in its body"));
}

CPPL_TEST(an_assumption_is_renamed_in_the_statements_that_name_it_and_no_other_proofs) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    const std::string text = "pure unsigned identity(unsigned x) {\n"
                             "    return x;\n"
                             "}\n"
                             "\n"
                             "law identity_at_zero(unsigned x)\n"
                             "    expects (x == 0u)\n"
                             "    proves (identity(x) == 0u);\n"
                             "\n"
                             "proof identity_at_zero_holds(unsigned x)\n"
                             "    proves (identity_at_zero(x))\n"
                             "{\n"
                             "    assume h : x == 0u;\n"
                             "    rewrite h;\n"
                             "    refl;\n"
                             "}\n"
                             "\n"
                             "law zero_is_the_input(unsigned x)\n"
                             "    expects (x == 0u)\n"
                             "    proves (0u == x);\n"
                             "\n"
                             "proof zero_is_the_input_holds(unsigned x)\n"
                             "    proves (zero_is_the_input(x))\n"
                             "{\n"
                             "    assume h : x == 0u;\n"
                             "    rewrite h;\n"
                             "    refl;\n"
                             "}\n";
    open(server, "file:///work/assume.cpp", text);
    const auto edit = rename(server, "file:///work/assume.cpp", position_of(text, "rewrite h", 8), "at_zero");
    CPPL_CHECK_EQ(refusal(edit), std::string("renamed"));
    if (edit.has_value()) {
        const std::string after = renamed(*edit, {{"assume.cpp", text}}).at("assume.cpp");
        const std::size_t second = after.find("proof zero_is_the_input_holds");
        CPPL_CHECK(after.find("assume at_zero : x == 0u;\n    rewrite at_zero;") < second);
        // The next proof binds its own `h`, which is another name.
        CPPL_CHECK(after.find("assume h : x == 0u;\n    rewrite h;", second) != std::string::npos);
    }
}

CPPL_TEST(a_law_is_renamed_where_it_is_declared_stated_and_used_as_evidence) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    const std::string text = "pure unsigned zero() {\n"
                             "    return 0u;\n"
                             "}\n"
                             "\n"
                             "trusted law adds_nothing(unsigned x)\n"
                             "    proves (x + zero() == x);\n"
                             "\n"
                             "proof adds_nothing_twice(unsigned y)\n"
                             "    proves (y + zero() == y)\n"
                             "{\n"
                             "    exact adds_nothing(y);\n"
                             "}\n";
    open(server, "file:///work/laws.cpp", text);
    const auto edit = rename(server, "file:///work/laws.cpp", position_of(text, "adds_nothing(y)"), "keeps_value");
    CPPL_CHECK_EQ(refusal(edit), std::string("renamed"));
    if (edit.has_value()) {
        const std::string after = renamed(*edit, {{"laws.cpp", text}}).at("laws.cpp");
        CPPL_CHECK(after.find("trusted law keeps_value(unsigned x)") != std::string::npos);
        CPPL_CHECK(after.find("exact keeps_value(y);") != std::string::npos);
        // A proof whose name only begins with the Law's is another name.
        CPPL_CHECK(after.find("proof adds_nothing_twice(unsigned y)") != std::string::npos);
    }
    // Renamed to a C++L word, the Law's own name would be read as that word.
    CPPL_CHECK_EQ(refusal(rename(server, "file:///work/laws.cpp", position_of(text, "adds_nothing(y)"), "proves")),
                  std::string("'proves' is a C++L word, and a place to rename in /work/laws.cpp lies inside C++L"));
}
