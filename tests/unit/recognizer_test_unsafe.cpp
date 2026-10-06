// Recognizer tests (unit_recognizer_test): unsafe blocks, units that
// import modules, and ghost declarations.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/source/location.hpp"
#include "cppl/testing/test.hpp"
#include "recognizer_test_support.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

using namespace recognizer_test_detail;

// SPEC: UNSAFE-001, WORD-011
// An unsafe block records where `unsafe` was written, the verified body it
// stands in if any, and whether another unsafe block holds it.
CPPL_TEST(unsafe_blocks_are_recognized_with_the_body_that_holds_them) {
    Recognized result;
    recognize("unsafe unsigned read_device();\n"
              "verified unsigned f(unsigned x) ensures (result == x) {\n"
              "    unsigned y = 0u;\n"
              "    unsafe { y = read_device(); unsafe { y = y + 1u; } }\n"
              "    return x;\n"
              "}\n"
              "int main() { unsafe { return static_cast<int>(read_device()); } }\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.engine.diagnostics().empty());
    CPPL_CHECK_EQ(result.syntax.unsafe_functions.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.unsafe_functions[0].function_name, "read_device");
    CPPL_CHECK_EQ(result.syntax.unsafe_functions[0].function_location.line, 1u);
    CPPL_CHECK_EQ(result.syntax.unsafe_blocks.size(), std::size_t{3});
    const auto& outer = result.syntax.unsafe_blocks[0];
    CPPL_CHECK(outer.function_index == std::optional<std::size_t>{0});
    CPPL_CHECK(!outer.nested);
    CPPL_CHECK_EQ(outer.location.line, 4u);
    CPPL_CHECK_EQ(outer.location.column, 5u);
    CPPL_CHECK(result.syntax.unsafe_blocks[1].nested);
    CPPL_CHECK(!result.syntax.unsafe_blocks[2].function_index.has_value());
    CPPL_CHECK(!result.syntax.unsafe_blocks[2].nested);
}

// SPEC: WORD-002, WORD-011, WORD-018
// `unsafe {x}` constructs a temporary wherever `unsafe` names a type, so the
// word used for anything else leaves every block and declaration ordinary C++.
// A warning says so only where an unsafe boundary could have been meant: in a
// verified body, before braces holding a statement, which no initializer
// holds, and on a declaration. A temporary in an ordinary function is ordinary
// C++ and nothing more.
CPPL_TEST(unsafe_named_anywhere_else_keeps_blocks_and_declarations_ordinary_cpp) {
    Recognized result;
    recognize("struct unsafe { unsafe(int) {} };\n"
              "void f() { unsafe {1}; unsafe{}; }\n",
              result);
    CPPL_CHECK(result.engine.diagnostics().empty());
    CPPL_CHECK(result.syntax.unsafe_blocks.empty());
    CPPL_CHECK(result.syntax.unsafe_functions.empty());

    for (const char* text : {"struct unsafe { unsafe(int) {} };\n"
                             "verified unsigned f(unsigned x) ensures (result == x) { unsafe {1}; return x; }\n",
                             "struct unsafe { unsafe(int) {} };\nvoid g();\nvoid f() { unsafe { g(); } }\n",
                             "struct unsafe { unsafe(int) {} };\nunsafe unsigned read_device();\n"}) {
        Recognized warned;
        recognize(text, warned);
        CPPL_CHECK(!warned.engine.has_errors());
        CPPL_CHECK(warned.syntax.unsafe_blocks.empty());
        CPPL_CHECK(warned.syntax.unsafe_functions.empty());
        CPPL_CHECK_EQ(warned.engine.diagnostics().size(), std::size_t{1});
        CPPL_CHECK(warned.engine.diagnostics()[0].severity == cppl::diagnostics::Severity::Warning);
    }
}

// SPEC: WORD-019, MODULE-001
// A module the unit imports may declare any word, in text the recognizer never
// reads, so each statement whose C++L reading rests on the word naming nothing
// else keeps its C++ meaning there. In a verified body a warning says so and
// points at the import.
CPPL_TEST(a_unit_importing_a_module_keeps_every_word_led_statement_ordinary_cpp) {
    for (const char* text : {"import words;\nint main() { contradiction verdict; cases c{3}; return c.v; }\n",
                             "export import words;\nvoid f() { decompose d{1}; ghost g; unsafe { run(); } }\n",
                             "import :part;\nvoid f() { validate<int>(1); }\n",
                             "module;\nexport module m;\nimport <vector>;\nvoid f() { contradiction v; }\n"}) {
        Recognized result;
        recognize(text, result);
        CPPL_CHECK(!result.engine.has_errors());
        CPPL_CHECK(result.syntax.path_contradictions.empty());
        CPPL_CHECK(result.syntax.path_splits.empty());
        CPPL_CHECK(result.syntax.ghost_declarations.empty());
        CPPL_CHECK(result.syntax.unsafe_blocks.empty());
        CPPL_CHECK(result.syntax.validations.empty());
    }

    Recognized verified;
    recognize("import words;\n"
              "verified unsigned f(unsigned x) ensures (result == x) { contradiction verdict; return x; }\n",
              verified);
    CPPL_CHECK(!verified.engine.has_errors());
    CPPL_CHECK(verified.syntax.path_contradictions.empty());
    CPPL_CHECK_EQ(verified.engine.diagnostics().size(), std::size_t{1});
    const auto& warning = verified.engine.diagnostics()[0];
    CPPL_CHECK(warning.severity == cppl::diagnostics::Severity::Warning);
    CPPL_CHECK(warning.message.find("may name an entity of a module") != std::string::npos);
    CPPL_CHECK_EQ(warning.notes.size(), std::size_t{1});
    CPPL_CHECK_EQ(warning.notes[0].location.line, 1u);

    // A type named `import` used in a declaration imports nothing, so the claim
    // in a function that is not verified is still refused.
    Recognized declared;
    recognize("struct import {};\nconst import k{};\nvoid f() { contradiction v; }\n", declared);
    CPPL_CHECK(declared.engine.has_errors());

    // An identifier named `import` is not a module import.
    Recognized named;
    recognize("int import = 0;\n"
              "verified unsigned f(unsigned x) ensures (result == x) {\n"
              "    if (x > x) { contradiction pinned(x); }\n"
              "    return x;\n"
              "}\n"
              "proof pinned(unsigned v) proves (v == v) { refl; }\n",
              named);
    CPPL_CHECK_EQ(named.syntax.path_contradictions.size(), std::size_t{1});
}

// SPEC: UNSAFE-002
CPPL_TEST(unsafe_combined_with_verified_or_pure_is_refused) {
    for (const char* text :
         {"unsafe verified unsigned f(unsigned x) ensures (result == x) { return x; }\n",
          "verified unsafe unsigned f(unsigned x) ensures (result == x) { return x; }\n",
          "unsafe pure unsigned f(unsigned x) { return x; }\n", "pure unsafe unsigned f(unsigned x) { return x; }\n"}) {
        Recognized result;
        recognize(text, result);
        CPPL_CHECK(result.engine.has_errors());
        CPPL_CHECK(result.syntax.unsafe_functions.empty());
        CPPL_CHECK(result.syntax.pure_markers.empty());
    }
}

// SPEC: UNSAFE-003, UNSAFE-004
// Nothing checks an unsafe function, so a contract written on one is refused
// rather than becoming a fact its callers rest on.
CPPL_TEST(a_contract_on_an_unsafe_function_is_refused) {
    Recognized result;
    recognize("unsafe unsigned read_device()\n    ensures (result < 10u);\n", result);
    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.unsafe_functions.empty());
    CPPL_CHECK_EQ(result.engine.diagnostics()[0].message, "an unsafe function states no contract");
}

// SPEC: UNSAFE-001
CPPL_TEST(an_unsafe_member_function_is_refused_rather_than_half_handled) {
    Recognized result;
    recognize("struct S { unsafe unsigned read(); };\n", result);
    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.unsafe_functions.empty());
}

// SPEC: UNSAFE-003
// An unsafe block's statements are not a path the verifier walks, so proof
// syntax written there would state what nothing checks.
CPPL_TEST(proof_syntax_inside_an_unsafe_block_is_refused) {
    for (const char* text : {"verified unsigned f(unsigned n) ensures (result == 0u) {\n"
                             "    unsafe { unsigned i = 0u; while (i < n) invariant (i <= n) { ++i; } }\n"
                             "    return 0u;\n"
                             "}\n",
                             "proof pinned(unsigned v) proves (v == v) { refl; }\n"
                             "verified unsigned f(unsigned x) ensures (result == x) {\n"
                             "    unsafe { if (x > x) { contradiction pinned(x); } }\n"
                             "    return x;\n"
                             "}\n"}) {
        Recognized result;
        recognize(text, result);
        CPPL_CHECK(result.engine.has_errors());
    }
}

// SPEC: GHOST-001, ERASE-011
// A ghost declaration records its word, the verified body it stands in, and
// everything that leaves the program: the whole declaration through its `;`.
CPPL_TEST(a_ghost_declaration_is_recognized_with_everything_that_erases) {
    Recognized result;
    const std::string text = "verified unsigned f(unsigned x) ensures (result == x) {\n"
                             "    ghost unsigned seen = x, twice = seen + seen;\n"
                             "    return x;\n"
                             "}\n";
    recognize(text, result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.engine.diagnostics().empty());
    CPPL_CHECK_EQ(result.syntax.ghost_declarations.size(), std::size_t{1});
    const auto& ghost = result.syntax.ghost_declarations[0];
    CPPL_CHECK_EQ(ghost.function_index, std::size_t{0});
    CPPL_CHECK_EQ(ghost.location.line, 2u);
    CPPL_CHECK_EQ(ghost.location.column, 5u);
    CPPL_CHECK_EQ(text.substr(ghost.erased.offset, ghost.erased.length),
                  std::string("ghost unsigned seen = x, twice = seen + seen;"));
}

// SPEC: WORD-002, WORD-011, WORD-018
// `ghost x = y;` declares `x` wherever `ghost` names a type, so the word used
// for anything else leaves every such declaration ordinary C++. Ghost state
// exists only in a verified body, so a warning says so there and nowhere else.
CPPL_TEST(ghost_named_anywhere_else_keeps_the_declaration_ordinary_cpp) {
    Recognized result;
    recognize("struct ghost { unsigned value; };\n"
              "verified unsigned f(unsigned x) ensures (result == x) { ghost g{x}; return g.value; }\n"
              "unsigned g(unsigned x) { ghost h{x}; ghost k = h; return k.value; }\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.ghost_declarations.empty());
    CPPL_CHECK_EQ(result.engine.diagnostics().size(), std::size_t{1});
    CPPL_CHECK(result.engine.diagnostics()[0].severity == cppl::diagnostics::Severity::Warning);
    CPPL_CHECK_EQ(result.engine.diagnostics()[0].location.line, 2u);
}

// SPEC: GHOST-001
// Where a ghost declaration may not stand: at namespace scope, in a class, in an
// ordinary function, as a statement's body, and without a type.
CPPL_TEST(a_ghost_declaration_outside_a_verified_block_is_refused) {
    for (const char* text : {"ghost unsigned counter = 0u;\n", "struct S { ghost unsigned member = 0u; };\n",
                             "unsigned f(unsigned x) { ghost unsigned g = x; return x; }\n",
                             "verified unsigned f(unsigned x) ensures (result == x) {\n"
                             "    if (x > 1u) ghost unsigned g = x;\n"
                             "    return x;\n"
                             "}\n",
                             "verified unsigned f(unsigned x) ensures (result == x) {\n"
                             "    unsigned y = x;\n"
                             "    ghost y = 1u;\n"
                             "    return x;\n"
                             "}\n"}) {
        Recognized result;
        recognize(text, result);
        CPPL_CHECK(result.engine.has_errors());
        CPPL_CHECK(result.syntax.ghost_declarations.empty());
    }
}
