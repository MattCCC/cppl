// LSP server lifecycle tests

#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/testing/test.hpp"

#include <string>
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
