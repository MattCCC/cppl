// Navigation by the names proof statements use, as the compiler resolved them
// (src/lsp/include/cppl/lsp/proof_names.hpp).

#include "cppl/clang/editor.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/lsp/document.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/proof_names.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/source/location.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace cppl;
using namespace cppl::lsp;
using clangbridge::Destination;

namespace {

const std::string kFixtures = CPPL_TEST_FIXTURES_DIR;

std::string read_fixture(const std::string& name) {
    std::ifstream stream(kFixtures + "/" + name, std::ios::binary);
    if (!stream) {
        ::cppl::testing::fail(__FILE__, __LINE__, "could not read fixture '" + name + "'");
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

Server fixture_server() {
    return Server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20", "-I" + kFixtures});
}

void open(Server& server, const std::string& uri, const std::string& text) {
    TextDocumentItem item;
    item.uri = uri;
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);
}

// Where `needle` starts in `text`, `into` bytes further on.
Position position_of(const std::string& text, const std::string& needle, std::size_t into = 0) {
    const std::size_t at = text.find(needle);
    if (at == std::string::npos) {
        ::cppl::testing::fail(__FILE__, __LINE__, "no '" + needle + "' in the text");
    }
    return PositionMapper(text).byte_offset_to_position(at + into);
}

std::string describe(const Position& position) {
    return std::to_string(position.line) + ":" + std::to_string(position.character);
}

std::string starts(const std::optional<std::vector<Location>>& locations) {
    if (!locations.has_value()) {
        return "null";
    }
    std::string out;
    for (const Location& location : *locations) {
        out += (out.empty() ? "" : " ") + describe(location.range.start);
    }
    return out;
}

std::string definition(Server& server, const std::string& uri, const Position& position) {
    TextDocumentIdentifier id;
    id.uri = uri;
    return starts(server.text_document_navigate(Destination::Definition, id, position));
}

std::string references(Server& server, const std::string& uri, const Position& position) {
    TextDocumentIdentifier id;
    id.uri = uri;
    return starts(server.text_document_references(id, position, true));
}

} // namespace

CPPL_TEST(apply_and_exact_lead_to_the_proof_they_name) {
    Server server = fixture_server();
    const std::string text = read_fixture("written_proof.cpp");
    open(server, "file:///written_proof.cpp", text);
    const std::string declared = describe(position_of(text, "proof identity_returns_input_holds", 6));
    CPPL_CHECK_EQ(definition(server, "file:///written_proof.cpp", position_of(text, "apply identity_returns", 6)),
                  declared);
    CPPL_CHECK_EQ(definition(server, "file:///written_proof.cpp", position_of(text, "exact identity_returns", 6)),
                  declared);
    // From the declaration, and from either use: the declaration and both uses.
    const std::string all = declared + " " + describe(position_of(text, "apply identity_returns", 6)) + " " +
                            describe(position_of(text, "exact identity_returns", 6));
    CPPL_CHECK_EQ(
        references(server, "file:///written_proof.cpp", position_of(text, "proof identity_returns_input_holds", 8)),
        all);
    CPPL_CHECK_EQ(references(server, "file:///written_proof.cpp", position_of(text, "exact identity_returns", 6)), all);
    // A statement's keyword names nothing.
    CPPL_CHECK(definition(server, "file:///written_proof.cpp", position_of(text, "refl;")).empty());
}

CPPL_TEST(a_trusted_law_named_as_evidence_leads_to_the_law) {
    Server server = fixture_server();
    const std::string text = read_fixture("trust_closure.cpp");
    open(server, "file:///trust_closure.cpp", text);
    const Position law = position_of(text, "trusted law sensor_identity", 12);
    CPPL_CHECK_EQ(definition(server, "file:///trust_closure.cpp", position_of(text, "exact sensor_identity", 6)),
                  describe(law));
    // Every proof statement naming it, and the Law itself.
    const std::string found = references(server, "file:///trust_closure.cpp", law);
    CPPL_CHECK(found.starts_with(describe(law)));
    CPPL_CHECK(found.find(describe(position_of(text, "exact sensor_identity(x)", 6))) != std::string::npos);
    CPPL_CHECK(found.find(describe(position_of(text, "exact sensor_identity(y)", 6))) != std::string::npos);
    CPPL_CHECK(found.find(describe(position_of(text, "rewrite sensor_identity(x)", 8))) != std::string::npos);
}

CPPL_TEST(an_assumption_named_by_a_statement_leads_to_its_assume) {
    Server server = fixture_server();
    const std::string text = read_fixture("rewritten_proof.cpp");
    open(server, "file:///rewritten_proof.cpp", text);
    const Position bound = position_of(text, "assume h", 7);
    CPPL_CHECK_EQ(definition(server, "file:///rewritten_proof.cpp", position_of(text, "rewrite h", 8)),
                  describe(bound));
    // The next proof binds its own `h`, which is another name.
    TextDocumentIdentifier id;
    id.uri = "file:///rewritten_proof.cpp";
    const auto highlights = server.text_document_document_highlight(id, bound);
    CPPL_CHECK(highlights.has_value());
    CPPL_CHECK_EQ(highlights->size(), std::size_t{2});
    CPPL_CHECK_EQ(describe(highlights->at(0).range.start), describe(bound));
    CPPL_CHECK(highlights->at(0).kind == DocumentHighlightKind::Text);
    CPPL_CHECK_EQ(describe(highlights->at(1).range.start), describe(position_of(text, "rewrite h", 8)));
    CPPL_CHECK(highlights->at(1).kind == DocumentHighlightKind::Read);
}

CPPL_TEST(a_claim_in_a_verified_body_leads_to_the_proof_it_names) {
    Server server = fixture_server();
    const std::string text = read_fixture("impossible_path.cpp");
    open(server, "file:///impossible_path.cpp", text);
    CPPL_CHECK_EQ(definition(server, "file:///impossible_path.cpp", position_of(text, "contradiction nothing", 14)),
                  describe(position_of(text, "proof nothing", 6)));
    CPPL_CHECK_EQ(definition(server, "file:///impossible_path.cpp", position_of(text, "contradiction pinned", 14)),
                  describe(position_of(text, "proof pinned", 6)));
}

CPPL_TEST(an_omitted_case_leads_to_the_evidence_it_names) {
    Server server = fixture_server();
    const std::string text = read_fixture("omitted_case.cpp");
    open(server, "file:///omitted_case.cpp", text);
    CPPL_CHECK_EQ(definition(server, "file:///omitted_case.cpp",
                             position_of(text, "omit unnamed by contradiction impossible", 30)),
                  describe(position_of(text, "assume impossible", 7)));
}

CPPL_TEST(a_record_the_buffer_no_longer_spells_answers_nothing) {
    // Records come from the last compile. One whose name the buffer no longer
    // spells where it was recorded, or spells only as part of a longer name,
    // is never used.
    DocumentManager documents;
    TextDocumentItem item;
    item.uri = "file:///stale.cpp";
    item.text = "proof pp()\n"
                "    proves (true)\n"
                "{\n"
                "    exact pp;\n"
                "}\n";
    documents.open(item);
    Document* document = documents.get("file:///stale.cpp");
    CPPL_CHECK(document != nullptr);
    const std::string path = document->path();
    document->set_resolved_names(
        {elaboration::ResolvedName{elaboration::ResolvedName::Kind::Proof, "p", source::SourceLocation{path, 4, 11},
                                   source::SourceLocation{path, 1, 7}},
         elaboration::ResolvedName{elaboration::ResolvedName::Kind::Proof, "q", source::SourceLocation{path, 4, 11},
                                   source::SourceLocation{path, 1, 7}}});
    const ProofNames names(documents);
    // At `pp` in `exact pp;`: neither record spells it.
    CPPL_CHECK(!names.declaration_at(*document, Position{3, 10}).has_value());
    CPPL_CHECK(!names.locate(ProofNames::Declaration{source::SourceLocation{path, 1, 7}, "p"}).has_value());
    CPPL_CHECK(names.uses_of(ProofNames::Declaration{source::SourceLocation{path, 1, 7}, "p"}).empty());
}
