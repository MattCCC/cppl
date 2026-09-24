// Verification status in the editor: a lens over each Law, proof and verified
// function, and the verdicts in hover, copied from the kernel's verdicts
// (src/lsp/include/cppl/lsp/verification.hpp).

#include "cppl/driver/buffer_compile.hpp"
#include "cppl/lsp/document.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/lsp/verification.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/status.hpp"
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

Server make_server() {
    return Server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20", "-I" + kFixtures});
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

std::vector<CodeLens> lenses(Server& server, const std::string& uri) {
    TextDocumentIdentifier id;
    id.uri = uri;
    return server.text_document_code_lens(id).value_or(std::vector<CodeLens>{});
}

// The lens over the name at `position`, or "none".
std::string lens_at(const std::vector<CodeLens>& found, const Position& position) {
    for (const CodeLens& lens : found) {
        if (lens.range.start.line == position.line && lens.range.start.character == position.character) {
            return lens.title;
        }
    }
    return "none";
}

std::string hover_text(Server& server, const std::string& uri, const Position& position) {
    TextDocumentIdentifier id;
    id.uri = uri;
    const std::optional<Hover> found = server.text_document_hover(id, position);
    return found.has_value() ? found->contents : std::string("none");
}

bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

CPPL_TEST(a_law_the_kernel_accepted_is_proven_and_a_false_one_is_not) {
    // The matched pair: the same shape of declaration, one true and one false.
    Server server = make_server();
    const std::string proven = read_fixture("identity_law.cpp");
    const std::string refused = read_fixture("false_law.cpp");
    open(server, "file:///identity_law.cpp", proven);
    open(server, "file:///false_law.cpp", refused);
    CPPL_CHECK_EQ(
        lens_at(lenses(server, "file:///identity_law.cpp"), position_of(proven, "law identity_returns_input", 4)),
        std::string("PROVEN"));
    const std::string title =
        lens_at(lenses(server, "file:///false_law.cpp"), position_of(refused, "law add_one_changes_nothing", 4));
    CPPL_CHECK(title.starts_with("UNRESOLVED"));
    CPPL_CHECK(!has(title, "PROVEN"));
    // Hover states the verdict, the goal, and why it is not proven.
    const std::string shown =
        hover_text(server, "file:///false_law.cpp", position_of(refused, "law add_one_changes_nothing", 4));
    CPPL_CHECK(has(shown, "**UNRESOLVED** law proposition"));
    CPPL_CHECK(has(shown, "Goal: `"));
    CPPL_CHECK(has(shown, "Why: "));
    CPPL_CHECK(!has(shown, "**PROVEN**"));
}

CPPL_TEST(a_proof_and_the_laws_it_proves_are_proven_with_their_evidence) {
    Server server = make_server();
    const std::string text = read_fixture("written_proof.cpp");
    open(server, "file:///written_proof.cpp", text);
    const std::vector<CodeLens> found = lenses(server, "file:///written_proof.cpp");
    CPPL_CHECK_EQ(lens_at(found, position_of(text, "law identity_returns_input", 4)), std::string("PROVEN"));
    // A proof of a Law has no obligation of its own: it is the Law's evidence.
    CPPL_CHECK_EQ(lens_at(found, position_of(text, "proof identity_returns_input_holds", 6)),
                  std::string("PROVEN as the evidence for law identity_returns_input"));
    const std::string shown =
        hover_text(server, "file:///written_proof.cpp", position_of(text, "proof identity_returns_input_holds", 6));
    CPPL_CHECK(has(shown, "**PROVEN** law proposition"));
    CPPL_CHECK(has(shown, "Evidence: written proof 'identity_returns_input_holds', accepted by the kernel"));
}

CPPL_TEST(a_refused_proof_leaves_its_law_and_itself_unproven) {
    // The evidence written for a false Law does not hold, and neither lens may
    // say otherwise.
    Server server = make_server();
    const std::string text = "pure unsigned plus_one(unsigned x) {\n"
                             "    return x + 1u;\n"
                             "}\n"
                             "law off_by_one(unsigned x)\n"
                             "    proves (plus_one(x) == x);\n"
                             "proof off_by_one_holds(unsigned x)\n"
                             "    proves (off_by_one(x))\n"
                             "{\n"
                             "    refl;\n"
                             "}\n";
    open(server, "file:///work/refused.cpp", text);
    const std::vector<CodeLens> found = lenses(server, "file:///work/refused.cpp");
    const std::string law = lens_at(found, position_of(text, "law off_by_one", 4));
    const std::string proof = lens_at(found, position_of(text, "proof off_by_one_holds", 6));
    // The kernel refused the evidence the proof supplied, so the proof is what
    // left the Law open.
    CPPL_CHECK(law.starts_with("UNRESOLVED"));
    CPPL_CHECK(proof.starts_with("UNRESOLVED"));
    CPPL_CHECK(has(proof, "as the evidence for law off_by_one"));
    CPPL_CHECK(!has(law, "PROVEN"));
    CPPL_CHECK(!has(proof, "PROVEN"));
}

CPPL_TEST(a_trusted_law_is_trusted_and_what_rests_on_it_says_so) {
    Server server = make_server();
    const std::string text = read_fixture("trust_closure.cpp");
    open(server, "file:///trust_closure.cpp", text);
    const std::vector<CodeLens> found = lenses(server, "file:///trust_closure.cpp");
    CPPL_CHECK_EQ(lens_at(found, position_of(text, "trusted law sensor_identity", 12)),
                  std::string("TRUSTED: an explicit assumption"));
    // SPEC: TRUSTED-003
    // A memory proposition has no obligation, and is shown TRUSTED all the same,
    // never proven and never unverified.
    CPPL_CHECK_EQ(lens_at(found, position_of(text, "trusted law device_window", 12)),
                  std::string("TRUSTED: an explicit assumption"));
    const std::string first = lens_at(found, position_of(text, "proof first_link", 6));
    CPPL_CHECK(first.starts_with("PROVEN relative to trusted "));
    CPPL_CHECK(has(first, "sensor_identity"));
}

CPPL_TEST(a_verified_function_sums_up_its_obligations) {
    Server server = make_server();
    const std::string text = read_fixture("verified_functions.cpp");
    open(server, "file:///verified_functions.cpp", text);
    const std::string title =
        lens_at(lenses(server, "file:///verified_functions.cpp"), position_of(text, "verified unsigned inc", 18));
    CPPL_CHECK(title == "PROVEN" || title.starts_with("PROVEN: all "));
    const std::string shown =
        hover_text(server, "file:///verified_functions.cpp", position_of(text, "verified unsigned inc", 18));
    CPPL_CHECK(has(shown, "**verified function** `inc`"));
    CPPL_CHECK(has(shown, "**PROVEN** "));
}

CPPL_TEST(a_compile_that_stopped_before_verification_verifies_nothing) {
    Server server = make_server();
    const std::string text = read_fixture("cppl_with_cpp_type_error.cpp");
    open(server, "file:///cppl_with_cpp_type_error.cpp", text);
    CPPL_CHECK_EQ(lens_at(lenses(server, "file:///cppl_with_cpp_type_error.cpp"),
                          position_of(text, "law identity_returns_input", 4)),
                  std::string("not verified: the compile stopped before verification"));
}

CPPL_TEST(a_verdict_for_an_older_version_is_not_shown) {
    // The records a compile left describe the text it compiled. Once the
    // buffer has moved on, they are shown for nothing.
    DocumentManager documents;
    TextDocumentItem item;
    item.uri = "file:///stale.cpp";
    item.text = "law l(int x)\n    proves (x == x);\n";
    item.version = 2;
    documents.open(item);
    Document* document = documents.get("file:///stale.cpp");
    driver::ObligationRecord record;
    record.origin = obligations::Origin::LawProposition;
    record.subject = "l";
    record.location = source::SourceLocation{document->path(), 1, 5};
    record.status = obligations::Status::Proven;
    document->set_verification(true, {record}, 1);
    CPPL_CHECK(verification_lenses(*document).empty());
    document->set_verification(true, {record}, 2);
    const std::vector<CodeLens> current = verification_lenses(*document);
    CPPL_CHECK_EQ(current.size(), std::size_t{1});
    CPPL_CHECK_EQ(current.front().title, std::string("PROVEN"));
}

CPPL_TEST(lenses_of_an_unknown_document_are_null) {
    Server server = make_server();
    TextDocumentIdentifier id;
    id.uri = "file:///never-opened.cpp";
    CPPL_CHECK(!server.text_document_code_lens(id).has_value());
}
