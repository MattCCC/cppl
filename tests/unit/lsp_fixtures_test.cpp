// Fixture-driven integration tests for the LSP buffer-compile pipeline.
//
// This is the primary acceptance invariant from the cppl-lsp task spec: a
// valid C++L fixture must not receive bogus Clang diagnostics solely
// because it contains valid C++L syntax, and a genuine problem (a C++L
// syntax error, or a real ordinary-C++ type error) must still surface.
// These tests drive cppl::driver::compile_buffer directly, the same
// library call Server::publish_diagnostics makes, against real files under
// tests/fixtures/ -- the corpus the task requires the LSP to prove itself
// against -- rather than a subprocess/JSON-RPC round trip, to keep the
// suite fast and free of transport flakiness.

#include "cppl/driver/buffer_compile.hpp"
#include "cppl/testing/test.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace cppl;

namespace {

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

driver::BufferCompileOutcome compile_fixture(const std::string& name, diagnostics::Engine& engine,
                                             const std::vector<std::string>& extra_arguments = {},
                                             const std::string& standard = "-std=c++20") {
    driver::BufferCompileRequest request;
    request.virtual_path = std::string(CPPL_TEST_FIXTURES_DIR) + "/" + name;
    request.text = read_fixture(name);
    request.clang = CPPL_TEST_DEFAULT_CLANG;
    request.clang_arguments = extra_arguments;
    request.clang_arguments.push_back(standard);
    // Fixtures resolve #include relative to the fixtures directory
    // (verified_functions.cpp includes "include/verified_contract.hpp"),
    // and the buffer is compiled from a scratch copy, so the search path
    // has to be supplied explicitly.
    request.clang_arguments.push_back("-I" + std::string(CPPL_TEST_FIXTURES_DIR));
    return driver::compile_buffer(request, engine);
}

bool has_category(const diagnostics::Engine& engine, diagnostics::Category category) {
    for (const auto& diagnostic : engine.diagnostics()) {
        if (diagnostic.category == category) {
            return true;
        }
    }
    return false;
}

std::size_t count_category(const diagnostics::Engine& engine, diagnostics::Category category) {
    std::size_t count = 0;
    for (const auto& diagnostic : engine.diagnostics()) {
        if (diagnostic.category == category) {
            ++count;
        }
    }
    return count;
}

} // namespace

// --- valid: the primary acceptance invariant --------------------------

CPPL_TEST(valid_identity_law_produces_no_cpp_semantic_diagnostics) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("identity_law.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
}

CPPL_TEST(valid_verified_functions_produces_no_cpp_semantic_diagnostics) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("verified_functions.cpp", engine, {}, "-std=c++17");
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
}

CPPL_TEST(valid_refinement_types_produces_no_cpp_semantic_diagnostics) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("refinement_types.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
}

CPPL_TEST(valid_written_proof_produces_no_cpp_semantic_diagnostics) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("written_proof.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
}

CPPL_TEST(valid_true_arithmetic_law_produces_no_cpp_semantic_diagnostics) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("true_arithmetic_law.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
}

// --- ordinary C++, no C++L at all --------------------------------------

CPPL_TEST(ordinary_cpp_file_is_not_recognized_as_cppl) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("hello.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(!outcome.has_cppl);
    CPPL_CHECK_EQ(engine.diagnostics().size(), 0u);
}

// --- a genuine C++ type error must still surface -----------------------

CPPL_TEST(genuine_cpp_type_error_alongside_valid_cppl_is_reported) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("cppl_with_cpp_type_error.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK(has_category(engine, diagnostics::Category::CppSemantic));

    bool found_type_error = false;
    for (const auto& diagnostic : engine.diagnostics()) {
        if (diagnostic.category == diagnostics::Category::CppSemantic &&
            diagnostic.message.find("int") != std::string::npos) {
            found_type_error = true;
        }
    }
    CPPL_CHECK(found_type_error);
}

// --- a false Law is a proof failure, not a C++ semantic error ----------

CPPL_TEST(false_law_is_a_proof_failure_not_a_cpp_semantic_error) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("false_law.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
    CPPL_CHECK(engine.has_errors());
}

// --- malformed C++L must produce a CpplSyntax diagnostic ---------------

CPPL_TEST(malformed_law_produces_cppl_syntax_diagnostic) {
    diagnostics::Engine engine;
    driver::BufferCompileRequest request;
    request.virtual_path = "malformed.cpp";
    request.text = "law incomplete(int x) ensures(;\n"; // unbalanced: malformed proposition
    request.clang = CPPL_TEST_DEFAULT_CLANG;
    request.clang_arguments = {"-std=c++20"};

    const auto outcome = driver::compile_buffer(request, engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(engine.has_errors());
}

// --- a genuine ordinary-C++ syntax error, no C++L involved -------------

CPPL_TEST(ordinary_cpp_syntax_error_is_reported) {
    diagnostics::Engine engine;
    driver::BufferCompileRequest request;
    request.virtual_path = "broken.cpp";
    request.text = "int main( {\n  return 0;\n}\n"; // missing ')'
    request.clang = CPPL_TEST_DEFAULT_CLANG;
    request.clang_arguments = {"-std=c++20"};

    const auto outcome = driver::compile_buffer(request, engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(!outcome.has_cppl);
    CPPL_CHECK(engine.has_errors());
}

// --- preprocessing failure must not crash, must produce a diagnostic ---

CPPL_TEST(missing_include_does_not_crash_and_produces_a_diagnostic) {
    diagnostics::Engine engine;
    driver::BufferCompileRequest request;
    request.virtual_path = "missing_include.cpp";
    request.text = "#include \"this_header_does_not_exist_anywhere.hpp\"\nint main() { return 0; }\n";
    request.clang = CPPL_TEST_DEFAULT_CLANG;
    request.clang_arguments = {"-std=c++20"};

    const auto outcome = driver::compile_buffer(request, engine);
    // Preprocessing failure is reported as a diagnostic; the pipeline itself
    // must not crash (AGENTS.md 23, fail closed).
    CPPL_CHECK(!outcome.ok);
    CPPL_CHECK(engine.has_errors());
}

CPPL_TEST(unknown_clang_executable_does_not_crash_and_produces_a_diagnostic) {
    diagnostics::Engine engine;
    driver::BufferCompileRequest request;
    request.virtual_path = "anything.cpp";
    request.text = "int main() { return 0; }\n";
    request.clang = "cppl-lsp-test-clang-that-does-not-exist";
    request.clang_arguments = {"-std=c++20"};

    const auto outcome = driver::compile_buffer(request, engine);
    CPPL_CHECK(!outcome.ok);
    CPPL_CHECK(engine.has_errors());
}
