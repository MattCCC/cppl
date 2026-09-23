// LSP server lifecycle tests

#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace cppl::lsp;

CPPL_TEST(server_lifecycle) {
    Server server;

    CPPL_CHECK(!server.is_shutting_down());
    CPPL_CHECK(!server.should_exit());

    server.initialize();
    server.initialized();

    CPPL_CHECK(!server.is_shutting_down());
    CPPL_CHECK(!server.should_exit());

    server.shutdown();
    CPPL_CHECK(server.is_shutting_down());
    CPPL_CHECK(!server.should_exit());

    server.exit();
    CPPL_CHECK(server.should_exit());
}

CPPL_TEST(server_sync_kind) {
    Server server;
    CPPL_CHECK(server.sync_kind() == TextDocumentSyncKind::Full);
}

CPPL_TEST(server_document_open_publishes_diagnostics) {
    Server server;

    bool published = false;
    std::string published_uri;
    std::vector<Diagnostic> published_diagnostics;

    server.set_diagnostic_publisher([&](const std::string& uri, std::vector<Diagnostic> diags) {
        published = true;
        published_uri = uri;
        published_diagnostics = std::move(diags);
    });

    TextDocumentItem item;
    item.uri = "file:///test.cpp";
    item.text = "law bad(int x);"; // Missing ensures - should generate diagnostic
    item.version = 1;

    server.text_document_did_open(item);

    CPPL_CHECK(published);
    CPPL_CHECK_EQ(published_uri, "file:///test.cpp");
    CPPL_CHECK(!published_diagnostics.empty());
}

CPPL_TEST(a_diagnostic_points_where_the_author_wrote_it) {
    // The buffer is compiled from a scratch copy, preprocessed first, and the
    // preprocessor writes the run of spaces before `frobnicate` as one.
    Server server;
    std::vector<Diagnostic> published;
    server.set_diagnostic_publisher(
        [&published](const std::string&, std::vector<Diagnostic> diagnostics) { published = std::move(diagnostics); });

    TextDocumentItem item;
    item.uri = "file:///spaces.cpp";
    item.text = "proof p(int a)\n    proves (a == a)\n{\n    refl;      frobnicate a;\n}\n";
    item.version = 1;
    server.text_document_did_open(item);

    const auto refusal = std::ranges::find_if(published, [](const Diagnostic& diagnostic) {
        return diagnostic.message.find("'frobnicate' does not begin a proof statement") != std::string::npos;
    });
    CPPL_CHECK(refusal != published.end());
    if (refusal != published.end()) {
        CPPL_CHECK_EQ(refusal->range.start.line, 3u);
        CPPL_CHECK_EQ(refusal->range.start.character, 15u);
    }
}

CPPL_TEST(server_document_change_republishes) {
    Server server;

    int publish_count = 0;
    server.set_diagnostic_publisher([&](const std::string&, const std::vector<Diagnostic>&) { ++publish_count; });

    TextDocumentItem item;
    item.uri = "file:///test.cpp";
    item.text = "old";
    item.version = 1;
    server.text_document_did_open(item);

    int count_after_open = publish_count;

    VersionedTextDocumentIdentifier id;
    id.uri = "file:///test.cpp";
    id.version = 2;

    std::vector<TextDocumentContentChangeEvent> changes;
    TextDocumentContentChangeEvent change;
    change.text = "new";
    changes.push_back(change);

    server.text_document_did_change(id, changes);

    CPPL_CHECK(publish_count > count_after_open);
}

CPPL_TEST(server_document_close_clears_diagnostics) {
    Server server;

    std::string last_uri;
    std::vector<Diagnostic> last_diagnostics;

    server.set_diagnostic_publisher([&](const std::string& uri, std::vector<Diagnostic> diags) {
        last_uri = uri;
        last_diagnostics = std::move(diags);
    });

    TextDocumentItem item;
    item.uri = "file:///test.cpp";
    item.text = "content";
    item.version = 1;
    server.text_document_did_open(item);

    TextDocumentIdentifier id;
    id.uri = "file:///test.cpp";
    server.text_document_did_close(id);

    CPPL_CHECK_EQ(last_uri, "file:///test.cpp");
    CPPL_CHECK(last_diagnostics.empty());
}

CPPL_TEST(completion_and_hover_do_not_require_a_diagnostic_publisher) {
    // Opening a document runs the compile that records its decomposition
    // states. That compile must not be skipped when no publisher is
    // installed, or completion would offer nothing for a reason unrelated to
    // the document, and an empty answer would stop meaning "the compiler
    // confirmed no states here".
    Server server;

    TextDocumentItem item;
    item.uri = "file:///nopublisher.cpp";
    item.text = "int main() { return 0; }";
    item.version = 1;
    server.text_document_did_open(item);

    // Ordinary C++ with no `cases` in it: both answer emptily, and neither
    // faults on a document whose compile ran with no publisher attached.
    TextDocumentIdentifier id;
    id.uri = "file:///nopublisher.cpp";
    CPPL_CHECK(server.text_document_completion(id, Position{0, 4}).empty());
    CPPL_CHECK(!server.text_document_hover(id, Position{0, 4}).has_value());
}

namespace {

CodeActionRequest code_actions_at(const std::string& uri, Range range, std::vector<std::string> only,
                                  bool automatic = false) {
    CodeActionRequest request;
    request.document.uri = uri;
    request.range = range;
    request.only = std::move(only);
    request.automatic = automatic;
    return request;
}

bool offers(const std::vector<CodeAction>& actions, std::string_view kind) {
    return std::ranges::any_of(actions, [&](const CodeAction& action) { return action.kind == kind; });
}

} // namespace

CPPL_TEST(a_syntax_migration_is_offered_where_it_would_edit) {
    Server server;
    TextDocumentItem item;
    item.uri = "file:///migrate.cpp";
    item.text = "int identity(int x) { return x; }\nlaw l(int x) ensures (identity(x) == x);\n";
    item.version = 1;
    server.text_document_did_open(item);

    // The cursor inside `ensures`, which spans characters 13 to 20 of line 1.
    const std::vector<CodeAction> here =
        server.text_document_code_actions(code_actions_at(item.uri, Range{{1, 15}, {1, 15}}, {"quickfix"}));
    CPPL_CHECK_EQ(here.size(), std::size_t{1});
    CPPL_CHECK_EQ(here[0].title, "Replace Law 'ensures' with 'proves'");
    CPPL_CHECK_EQ(here[0].kind, "quickfix");
    CPPL_CHECK_EQ(here[0].edits.size(), std::size_t{1});
    CPPL_CHECK_EQ(here[0].edits[0].range.start.line, 1u);
    CPPL_CHECK_EQ(here[0].edits[0].range.start.character, 13u);
    CPPL_CHECK_EQ(here[0].edits[0].range.end.character, 20u);
    CPPL_CHECK_EQ(here[0].edits[0].newText, "proves");

    // Elsewhere in the file the migration is not offered.
    CPPL_CHECK(
        server.text_document_code_actions(code_actions_at(item.uri, Range{{0, 4}, {0, 4}}, {"quickfix"})).empty());
}

CPPL_TEST(canonical_formatting_is_a_fix_all_computed_only_when_asked_for) {
    // Formatting runs clang-format, so the editor's automatic requests as the
    // cursor moves never pay for it; asking by kind or by hand does.
    Server server;
    TextDocumentItem item;
    item.uri = "file:///fixall.cpp";
    item.text = "verified int f(int x) ensures (result == x) { return x; }\n";
    item.version = 1;
    server.text_document_did_open(item);
    const Range cursor{{0, 0}, {0, 0}};

    CPPL_CHECK(
        offers(server.text_document_code_actions(code_actions_at(item.uri, cursor, {"source.fixAll"})), kFixAllKind));
    CPPL_CHECK(offers(server.text_document_code_actions(code_actions_at(item.uri, cursor, {"source"})), kFixAllKind));
    CPPL_CHECK(offers(server.text_document_code_actions(code_actions_at(item.uri, cursor, {})), kFixAllKind));
    CPPL_CHECK(!offers(server.text_document_code_actions(code_actions_at(item.uri, cursor, {}, true)), kFixAllKind));
    CPPL_CHECK(
        !offers(server.text_document_code_actions(code_actions_at(item.uri, cursor, {"quickfix"})), kFixAllKind));
    // A kind is matched as a whole segment, not as a string prefix.
    CPPL_CHECK(!offers(server.text_document_code_actions(code_actions_at(item.uri, cursor, {"sour"})), kFixAllKind));
}

CPPL_TEST(code_actions_on_an_unknown_document_are_empty) {
    Server server;
    CPPL_CHECK(server.text_document_code_actions(code_actions_at("file:///never-opened.cpp", Range{}, {})).empty());
}

CPPL_TEST(completion_on_an_unknown_document_is_empty) {
    Server server;
    TextDocumentIdentifier id;
    id.uri = "file:///never-opened.cpp";
    CPPL_CHECK(server.text_document_completion(id, Position{0, 0}).empty());
    CPPL_CHECK(!server.text_document_hover(id, Position{0, 0}).has_value());
}
