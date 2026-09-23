// Document management tests

#include "cppl/lsp/document.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/testing/test.hpp"

#include <string>

using namespace cppl::lsp;

CPPL_TEST(document_creation_and_parsing) {
    std::string text = R"(
law test(int x) proves (x == x);
)";

    Document doc("file:///test.cpp", text, 1);

    CPPL_CHECK_EQ(doc.uri(), "file:///test.cpp");
    CPPL_CHECK_EQ(doc.text(), text);
    CPPL_CHECK_EQ(doc.version(), 1);
    CPPL_CHECK(doc.tokens() != nullptr);
    CPPL_CHECK(doc.syntax() != nullptr);
}

CPPL_TEST(document_update) {
    Document doc("file:///test.cpp", "old text", 1);
    CPPL_CHECK_EQ(doc.version(), 1);

    std::string new_text = "new text";
    doc.update(new_text, 2);

    CPPL_CHECK_EQ(doc.text(), new_text);
    CPPL_CHECK_EQ(doc.version(), 2);
}

CPPL_TEST(document_manager_open_and_get) {
    DocumentManager manager;

    TextDocumentItem item;
    item.uri = "file:///test1.cpp";
    item.text = "content1";
    item.version = 1;

    manager.open(item);

    Document* doc = manager.get("file:///test1.cpp");
    CPPL_CHECK(doc != nullptr);
    CPPL_CHECK_EQ(doc->uri(), "file:///test1.cpp");
    CPPL_CHECK_EQ(doc->text(), "content1");
    CPPL_CHECK_EQ(doc->version(), 1);
}

CPPL_TEST(document_manager_close) {
    DocumentManager manager;

    TextDocumentItem item;
    item.uri = "file:///test.cpp";
    item.text = "content";
    item.version = 1;

    manager.open(item);
    CPPL_CHECK(manager.get("file:///test.cpp") != nullptr);

    TextDocumentIdentifier id;
    id.uri = "file:///test.cpp";
    manager.close(id);

    CPPL_CHECK(manager.get("file:///test.cpp") == nullptr);
}

CPPL_TEST(document_manager_change) {
    DocumentManager manager;

    TextDocumentItem item;
    item.uri = "file:///test.cpp";
    item.text = "old";
    item.version = 1;
    manager.open(item);

    VersionedTextDocumentIdentifier id;
    id.uri = "file:///test.cpp";
    id.version = 2;

    std::vector<TextDocumentContentChangeEvent> changes;
    TextDocumentContentChangeEvent change;
    change.text = "new";
    changes.push_back(change);

    manager.change(id, changes);

    Document* doc = manager.get("file:///test.cpp");
    CPPL_CHECK(doc != nullptr);
    CPPL_CHECK_EQ(doc->text(), "new");
    CPPL_CHECK_EQ(doc->version(), 2);
}

CPPL_TEST(a_ranged_change_is_applied_where_it_lands) {
    // Full sync is advertised, but a client that sends a range anyway must have
    // it applied in place: taken as the whole document, the two characters
    // below would silently become the entire buffer.
    Document doc("file:///test.cpp", "int a = 1;\nint b = 2;\n", 1);
    TextDocumentContentChangeEvent change;
    change.range = Range{Position{1, 8}, Position{1, 9}};
    change.text = "42";
    doc.apply_change(change, 2);
    CPPL_CHECK_EQ(doc.text(), "int a = 1;\nint b = 42;\n");
    CPPL_CHECK_EQ(doc.version(), 2);
}

CPPL_TEST(a_ranged_change_counts_positions_in_utf16_units) {
    // `é` is one UTF-16 unit and two UTF-8 bytes, so `x` is at character 5
    // and byte 6.
    Document doc("file:///test.cpp", "// é x\n", 1);
    TextDocumentContentChangeEvent change;
    change.range = Range{Position{0, 5}, Position{0, 6}};
    change.text = "y";
    doc.apply_change(change, 2);
    CPPL_CHECK_EQ(doc.text(), "// é y\n");
}

CPPL_TEST(ranged_changes_apply_in_order_each_to_the_result_of_the_last) {
    DocumentManager manager;
    TextDocumentItem item;
    item.uri = "file:///test.cpp";
    item.text = "abc";
    item.version = 1;
    manager.open(item);

    TextDocumentContentChangeEvent insert;
    insert.range = Range{Position{0, 3}, Position{0, 3}};
    insert.text = "d";
    TextDocumentContentChangeEvent erase;
    erase.range = Range{Position{0, 0}, Position{0, 1}};
    manager.change(VersionedTextDocumentIdentifier{"file:///test.cpp", 2}, {insert, erase});

    const Document* doc = manager.get("file:///test.cpp");
    CPPL_CHECK(doc != nullptr);
    CPPL_CHECK_EQ(doc->text(), "bcd");
}

CPPL_TEST(document_parses_valid_cppl) {
    std::string text = R"(
law reflexivity(int x) proves (x == x);

proof prove_reflexivity(int x) proves(x == x) {
    refl;
}

verified int identity(int x) ensures(result == x) {
    return x;
}

type Positive = int where(self > 0);
)";

    Document doc("file:///test.cpp", text, 1);

    CPPL_CHECK(doc.syntax() != nullptr);
    CPPL_CHECK(doc.syntax()->laws.size() == 1);
    CPPL_CHECK(doc.syntax()->proofs.size() == 1);
    CPPL_CHECK(doc.syntax()->verified_functions.size() == 1);
    CPPL_CHECK(doc.syntax()->refinement_types.size() == 1);
}
