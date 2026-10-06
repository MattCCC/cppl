#pragma once

// The state the recognizer reads a unit in, and the token readers its passes
// share; each pass is defined in the recognizer_*.cpp file named for what
// it reads.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::frontend::detail::recognizer {

// Defined in recognizer.cpp.
bool is_type_keyword(const Token& token);

std::size_t specifiers_start(const std::vector<Token>& tokens, std::size_t index);

std::optional<std::size_t> template_header_start(const std::vector<Token>& tokens, std::size_t index);

bool at_declaration_start(const std::vector<Token>& tokens, std::size_t index);

std::size_t matching_parenthesis(const std::vector<Token>& tokens, std::size_t open);

std::size_t matching_angle_bracket(const std::vector<Token>& tokens, std::size_t open);

std::size_t matching_bracket(const std::vector<Token>& tokens, std::size_t open);

std::size_t matching_brace(const std::vector<Token>& tokens, std::size_t open);

std::optional<ClauseKind> clause_kind(const Token& token);

bool is_specification_clause(const Token& token);

inline void report(diagnostics::Engine& engine, const source::SourceLocation& location, diagnostics::Category category,
                   std::string message, std::string note = {}) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = category;
    diagnostic.message = std::move(message);
    diagnostic.location = location;
    if (!note.empty()) {
        diagnostic.notes.push_back(diagnostics::Note{std::move(note), diagnostic.location});
    }
    engine.report(std::move(diagnostic));
}

inline void report(diagnostics::Engine& engine, const TokenStream& stream, const Token& token,
                   diagnostics::Category category, std::string message, std::string note = {}) {
    report(engine, stream.location_of(token), category, std::move(message), std::move(note));
}

// Defined in recognizer.cpp.
void check_clause_sequence(const std::vector<Clause>& clauses, diagnostics::Engine& engine);

// `law` introduces a Law only when a contract clause follows the parameter
// list. Up to that point the token sequence is still ordinary C++ (a function
// returning a type named `law`, for instance), so nothing is reinterpreted
// until the clause makes the ordinary C++ reading impossible (SPEC.md 3.1).
bool read_proof_statements(const TokenStream& stream, std::size_t body_open, std::size_t body_close,
                           diagnostics::Engine& engine, std::vector<ProofStatement>& statements, unsigned nesting = 0,
                           bool draft = false);

// Reads the statements of the block from `body_open` to `body_close`, as
// `read_proof_statements` does. In a draft, a statement it cannot read is kept
// as `Unread` -- up to the `;` that ends it, or the `}` closing a block it
// opens -- and reading resumes after it, so the block is always read to its end.
bool read_block(const TokenStream& stream, std::size_t body_open, std::size_t body_close, diagnostics::Engine& engine,
                std::vector<ProofStatement>& statements, unsigned nesting, bool draft);

// Defined in recognizer.cpp.
source::ByteSpan tokens_span(const Token& first, const Token& last);

// Defined in recognizer_laws.cpp.
bool try_law(const TokenStream& stream, std::size_t index, diagnostics::Engine& engine, LawDeclaration& law,
             std::size_t& next_index, ProofDeclaration& body, RecognitionMode mode);

bool try_refinement_type(const TokenStream& stream, std::size_t index, diagnostics::Engine& engine,
                         RefinementType& refinement, std::size_t& next_index);

// Defined in recognizer_proofs.cpp.
bool try_proof(const TokenStream& stream, std::size_t index, diagnostics::Engine& engine, ProofDeclaration& proof,
               std::size_t& next_index, RecognitionMode mode);

// Defined in recognizer_declarators.cpp.
bool names_a_scope(const std::vector<Token>& tokens, std::size_t index);

bool is_specifier(const std::vector<Token>& tokens, std::size_t index, std::string_view word);

// The decl-specifiers C++ admits after a type name, before the declarator:
// `verified const c{};` and `verified static s;` declare variables of a type
// named `verified`, and `verified constexpr f();` a function returning one.
inline constexpr auto kSpecifiersAfterType = std::to_array<std::string_view>(
    {"const", "volatile", "static", "extern", "inline", "constexpr", "consteval", "constinit", "virtual", "friend",
     "mutable", "thread_local", "typedef", "register"});

// Defined in recognizer_declarators.cpp.
bool is_one_of(const Token& token, std::span<const std::string_view> words);

bool specifier_introduces_declaration(const std::vector<Token>& tokens, std::size_t index);

std::size_t declarator_parameters(const std::vector<Token>& tokens, std::size_t name);

bool conversion_function(const std::vector<Token>& tokens, std::size_t name);

std::string declarator_name_text(const std::vector<Token>& tokens, std::size_t name);

std::optional<std::size_t> find_declarator_name(const std::vector<Token>& tokens, std::size_t index);

std::size_t qualified_name_start(const std::vector<Token>& tokens, std::size_t name);

// Defined in recognizer_functions.cpp.
bool try_explicit_instantiation(const TokenStream& stream, const std::vector<Token>& tokens, std::size_t index,
                                ExplicitInstantiation& instantiation, std::size_t& next_index);

// Defined in recognizer_declarators.cpp.
std::size_t skip_ordinary_declarator_suffix(const std::vector<Token>& tokens, std::size_t cursor);

// Defined in recognizer_functions.cpp.
bool has_specification_clause(const std::vector<Token>& tokens, std::size_t name_index, std::size_t& clause_index);

std::optional<std::size_t> scan_function_clauses(const TokenStream& stream, std::size_t cursor,
                                                 diagnostics::Engine& engine, std::vector<Clause>& clauses);

bool has_cppl_keyword(const std::vector<Token>& tokens, std::size_t index, std::size_t name_index);

bool record_unchecked_clauses(const TokenStream& stream, std::size_t keyword_index, std::size_t name_index,
                              diagnostics::Engine& engine, Syntax& syntax);

bool written_between(const std::vector<Token>& tokens, std::size_t from, std::size_t to, std::string_view word);

void refuse_lifetime_member(const TokenStream& stream, const Token& at, bool destructor, diagnostics::Engine& engine);

bool refused_member(const TokenStream& stream, std::size_t index, std::size_t name, std::size_t close,
                    std::string_view class_name, diagnostics::Engine& engine);

// `verified` marks a function whose contract this implementation has to
// discharge. The clauses are delimited here; what they mean is settled once
// Clang has resolved them, like every other specification expression.
//
// `member` is whether the declaration stands in a class: a member function
// whose contract uses ordinary member lookup and `this` (SPEC.md CONTRACT-008).
// `refuse_unsupported` refuses here a member this implementation does not
// verify. A layout-only recognition keeps every well-formed declaration for the
// formatter, as it always has, so it does not refuse one.
inline bool try_verified(const TokenStream& stream, std::size_t index, diagnostics::Engine& engine,
                         VerifiedFunction& verified, std::size_t& next_index, bool member, bool refuse_unsupported,
                         std::string_view class_name = {}) {
    const std::vector<Token>& tokens = stream.tokens();
    next_index = index + 1;

    const std::optional<std::size_t> name = find_declarator_name(tokens, index);
    if (!name.has_value()) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "the 'verified' specifier applies to a function declaration",
               "no function declarator follows this specifier");
        return false;
    }
    // A qualified declarator redeclares a function declared elsewhere: a
    // member of its class, or a function of its namespace. That declaration
    // states the contract, and this definition inherits it (SPEC.md
    // CONTRACT-005). A contract stated here instead would be resolved in a
    // scope the declaration's members are not named in.
    if (refuse_unsupported && *name > 0 && tokens[*name - 1].is_punctuator("::")) {
        report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
               "'verified' is applied to a qualified declarator, which redeclares a function declared elsewhere",
               "state the contract on the function's declaration -- a member function's on its declaration in the "
               "class -- and define it here without 'verified' or clauses; the definition inherits the contract "
               "(SPEC.md CONTRACT-005, CLASS-008)");
        return false;
    }

    // An explicit specialization's declarator is `name<args>(...)`, so the
    // parameter list opens after the arguments rather than after the name, and
    // an operator function's opens after its operator: the `()` of
    // `operator()` is its name, not its parameters.
    const bool operator_function = tokens[*name].is_identifier("operator");
    // A conversion function names a type where an operator function names its
    // operator, and states no return type of its own for `result` to have.
    if (operator_function && conversion_function(tokens, *name)) {
        report(engine, stream, tokens[*name], diagnostics::Category::UnsupportedSemantics,
               "a verified conversion function is not modeled",
               "a conversion function states no return type before its name, so a contract would have no "
               "'result' type to read; state the property on a member function instead");
        return false;
    }
    std::size_t open = declarator_parameters(tokens, *name);
    if (!operator_function && open < tokens.size() && tokens[open].is_punctuator("<")) {
        const std::size_t arguments_close = matching_angle_bracket(tokens, open);
        if (arguments_close >= tokens.size()) {
            return false;
        }
        open = arguments_close + 1;
    }
    if (open >= tokens.size() || !tokens[open].is_punctuator("(")) {
        return false;
    }
    const std::size_t close = matching_parenthesis(tokens, open);
    if (close >= tokens.size()) {
        return false;
    }
    if (member && refuse_unsupported && refused_member(stream, index, *name, close, class_name, engine)) {
        return false;
    }

    // Whatever stands between the specifiers and the declarator is the return
    // type, and the contract's `result` is a value of it.
    std::size_t type_start = index + 1;
    const bool also_pure = is_specifier(tokens, type_start, "pure");
    if (also_pure) {
        ++type_start;
    }
    if (is_specifier(tokens, type_start, "unsafe")) {
        report(engine, stream, tokens[type_start], diagnostics::Category::CpplSyntax,
               "'unsafe' cannot be combined with 'verified'",
               "an unsafe function is not verified: it marks a boundary whose safety is not established, so it "
               "cannot also claim what 'verified' asks to be checked (SPEC.md UNSAFE-002)");
        return false;
    }
    if (type_start >= *name) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "a verified function states a return type before its name");
        return false;
    }
    // `auto` naming a trailing return type (`auto f(...) -> T`) still states
    // the return type explicitly, just after the parameter list rather than
    // before the name (GRAMMAR.md 43); only a genuinely deduced return type
    // (no `->` at all) leaves `result`'s type unwritten and unsupported.
    const bool trailing_return = close + 1 < tokens.size() && tokens[close + 1].is_punctuator("->");
    if (tokens[type_start].is_identifier("auto") && !trailing_return) {
        report(engine, stream, tokens[type_start], diagnostics::Category::UnsupportedSemantics,
               "a deduced return type is not supported on a verified function",
               "the contract's 'result' is a value of the declared return type, so this "
               "implementation requires one to be written");
        return false;
    }

    const std::size_t first_clause = skip_ordinary_declarator_suffix(tokens, close + 1);
    const std::optional<std::size_t> scanned = scan_function_clauses(stream, first_clause, engine, verified.clauses);
    if (!scanned.has_value()) {
        return false;
    }
    std::size_t cursor = *scanned;

    check_clause_sequence(verified.clauses, engine);
    const auto ensures_count = std::ranges::count_if(
        verified.clauses, [](const Clause& clause) { return clause.kind == ClauseKind::Ensures; });
    if (ensures_count > 1) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "verified function '" + declarator_name_text(tokens, *name) + "' has " + std::to_string(ensures_count) +
                   " ensures clauses",
               "a verified function has exactly one ensures clause");
        return false;
    }
    const bool declaration_only = cursor < tokens.size() && tokens[cursor].is_punctuator(";");
    if (cursor >= tokens.size() || (!tokens[cursor].is_punctuator("{") && !declaration_only)) {
        report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
               "verified function '" + declarator_name_text(tokens, *name) + "' is declared but not defined here",
               "its obligation comes from the body, so this implementation verifies a "
               "function where it is defined");
        return false;
    }
    const std::size_t body_close = declaration_only ? cursor : matching_brace(tokens, cursor);
    if (body_close >= tokens.size()) {
        return false;
    }

    verified.keyword = tokens[index].span;
    verified.keyword_location = stream.location_of(tokens[index]);
    // The header ends at its closing `>`, which is where the declaration's own
    // specifiers begin. Taking it to the `verified` keyword instead would carry
    // any `inline` or `static` between them into the generated probe, where
    // they do not belong.
    const std::size_t declaration_start = specifiers_start(tokens, index);
    if (const std::optional<std::size_t> header = template_header_start(tokens, declaration_start);
        header.has_value()) {
        verified.template_header = source::ByteSpan{tokens[*header].span.offset, tokens[declaration_start].span.offset -
                                                                                     tokens[*header].span.offset};
        // `template <>` declares no parameters, so the `<` is immediately
        // followed by its `>`: an explicit specialization rather than a
        // template.
        verified.explicit_specialization = *header + 2 < tokens.size() && tokens[*header + 1].is_punctuator("<") &&
                                           tokens[*header + 2].is_punctuator(">");
    }
    verified.function_name = declarator_name_text(tokens, *name);
    verified.function_location = stream.location_of(tokens[*name]);
    verified.function_offset = tokens[*name].span.offset;
    // A static member function has no implicit object, so its probes are
    // static members of the class and it is verified as a function is.
    verified.member = member;
    verified.static_member = member && written_between(tokens, declaration_start, *name, "static");
    verified.return_type =
        source::ByteSpan{tokens[type_start].span.offset, tokens[*name].span.offset - tokens[type_start].span.offset};
    verified.parameters =
        source::ByteSpan{tokens[open].span.end(), tokens[close].span.offset - tokens[open].span.end()};
    verified.clause_region = source::ByteSpan{tokens[first_clause].span.offset,
                                              tokens[cursor].span.offset - tokens[first_clause].span.offset};
    verified.body_end = tokens[body_close].span.end();
    verified.body_end_line = tokens[body_close].line;
    verified.body_end_column = tokens[body_close].column + 1;
    if (!declaration_only) {
        verified.body_open = tokens[cursor].span.end();
        verified.body_open_line = tokens[cursor].line;
        verified.body_open_column = tokens[cursor].column + 1;
    }

    // The body is walked as usual, so anything inside it is recognized exactly
    // as it would be in an ordinary function.
    next_index = declaration_only ? cursor + 1 : cursor;
    return true;
}

enum class ScopeKind : std::uint8_t {
    Namespace,
    Class,
    Block,
};

// Defined in recognizer_statements.cpp.
ScopeKind scope_kind_before(const std::vector<Token>& tokens, std::size_t brace);

std::string_view class_name_before(const std::vector<Token>& tokens, std::size_t brace);

enum class LoopClauses : std::uint8_t {
    None,
    Recognized,
    Refused,
};

// Defined in recognizer_statements.cpp.
bool holds_a_statement(const std::vector<Token>& tokens, std::size_t open, std::size_t close);

LoopClauses try_loop_clauses(const TokenStream& stream, std::size_t index, std::size_t clause_start,
                             diagnostics::Engine& engine, LoopSpecification& loop, std::size_t& next_index);

std::optional<std::size_t> contradiction_statement_end(const std::vector<Token>& tokens, std::size_t index);

std::optional<std::size_t> ghost_declaration_end(const std::vector<Token>& tokens, std::size_t index);

bool ghost_declares(const std::vector<Token>& tokens, std::size_t index, std::size_t end);

std::optional<std::size_t> split_statement_end(const std::vector<Token>& tokens, std::size_t index);

bool admit_split_arms(diagnostics::Engine& engine, const ProofStatement& statement);

void split_claims(const ProofStatement& statement,
                  std::vector<std::pair<const ProofStatement*, const ProofArm*>>& found);

std::optional<std::size_t> first_module_import(const std::vector<Token>& tokens);

bool at_statement_start(const std::vector<Token>& tokens, std::size_t index);

// What recognize() reads a unit in: its tokens, the syntax read so far, the
// scopes the scan stands in, and the statements written with a C++L word,
// which are decided once the whole unit has been read. Each pass is one
// member, run in the order recognize() runs them.
struct Recognizer {
    Recognizer(const TokenStream& scanned, diagnostics::Engine& reported, RecognitionMode requested)
        : stream(scanned),
          engine(reported),
          mode(requested),
          tokens(stream.tokens()),
          tolerant(mode != RecognitionMode::Compile) {}

    const TokenStream& stream;
    diagnostics::Engine& engine;
    RecognitionMode mode;
    const std::vector<Token>& tokens = stream.tokens();
    Syntax syntax;

    std::vector<ScopeKind> scopes;
    [[nodiscard]] bool at_namespace_scope() const {
        return std::ranges::all_of(scopes, [](ScopeKind kind) { return kind == ScopeKind::Namespace; });
    }
    // GRAMMAR.md 36/38: Laws, proofs and verified member contracts also have
    // class scope. This implementation verifies a member function's contract
    // (`at_member_scope` below) but not a Law or a proof declared in a class -
    // Compile mode keeps rejecting those exactly as before, unchanged by this
    // predicate - while the formatter (Edit mode) still needs to see and
    // canonically lay out the construct a developer wrote, the same way it
    // lays out any other syntactically well-formed but semantically
    // unsupported input.
    [[nodiscard]] bool at_layout_scope() const {
        return std::ranges::all_of(
            scopes, [](ScopeKind kind) { return kind == ScopeKind::Namespace || kind == ScopeKind::Class; });
    }
    // Directly in a class that is itself declared at namespace scope or in
    // another such class: where a member function declaration stands.
    [[nodiscard]] bool at_member_scope() const {
        return !scopes.empty() && scopes.back() == ScopeKind::Class && at_layout_scope();
    }
    // Edit and Draft keep what Compile refuses; Draft keeps more still.
    const bool tolerant;

    // The token range of each verified body, so a loop's clauses can be tied to
    // the function whose obligations they become.
    struct VerifiedBody {
        std::size_t open = 0;
        std::size_t close = 0;
        std::size_t function = 0;
    };
    std::vector<VerifiedBody> verified_bodies;

    // Statements spelled `contradiction name;` or `contradiction name(...);` in
    // a function body. Which of them are claims is decided once the whole unit
    // has been read, because that depends on every other use of the word.
    struct Written {
        std::size_t keyword = 0;
        std::size_t terminator = 0;
    };
    std::vector<Written> written_contradictions;
    // Statements spelled `cases subject { ... }` or `decompose subject { ... }`
    // in a function body, decided like claims once the unit has been read. Their
    // arms are read whole, so a claim inside one is the split's, never a
    // statement of its own.
    std::vector<Written> written_splits;
    // Statements spelled `unsafe { ... }`, and declarations led by `unsafe`,
    // decided the same way: `unsafe {x}` constructs a temporary wherever
    // `unsafe` names a type (SPEC.md 3.1, GRAMMAR.md 22, 23).
    std::vector<Written> written_unsafe_blocks;
    struct WrittenUnsafeDeclaration {
        std::size_t keyword = 0;
        bool namespace_scope = false;
    };
    std::vector<WrittenUnsafeDeclaration> written_unsafe_declarations;
    // Declarations led by `ghost`, decided the same way: `ghost x = y;` declares
    // `x` wherever `ghost` names a type (GRAMMAR.md 21). One written where no
    // statement of a block begins is recorded too, so it can be refused by name.
    struct WrittenGhost {
        std::size_t keyword = 0;
        std::size_t terminator = 0;
        bool in_block = false;
    };
    std::vector<WrittenGhost> written_ghosts;
    // The name of each class scope, beside `scopes`: what a constructor or a
    // destructor of the innermost class is named. Empty for every other scope.
    std::vector<std::string_view> scope_names;

    // A module the unit imports may declare any name, in text not read here
    // (SPEC.md MODULE-001). In a unit that imports one, no word can be shown
    // to name nothing else, so the import stands for the other use that keeps
    // each statement below ordinary C++ (WORD-019).
    std::optional<std::size_t> module_import;

    [[nodiscard]] bool in_proof(const Token& token) const {
        const auto covers = [&token](const source::ByteSpan& span) {
            return token.span.offset >= span.offset && token.span.offset < span.end();
        };
        return std::ranges::any_of(syntax.proofs,
                                   [&covers](const ProofDeclaration& proof) { return covers(proof.range.span); }) ||
               std::ranges::any_of(syntax.laws,
                                   [&covers](const LawDeclaration& law) { return covers(law.range.span); });
    }
    [[nodiscard]] bool in_verified_body(std::size_t at) const {
        return std::ranges::any_of(verified_bodies,
                                   [at](const VerifiedBody& body) { return body.open < at && at < body.close; });
    }
    [[nodiscard]] bool in_split(std::size_t at) const {
        return std::ranges::any_of(
            written_splits, [at](const Written& written) { return written.keyword <= at && at <= written.terminator; });
    }
    // Warns that the statement led by the word at `at` is ordinary C++, since
    // `other` uses the word or imports a module that may declare it.
    void warn_ordinary(std::size_t at, std::size_t other, std::string_view rest);
    // The first use of `word` outside laws and proofs that `claimed` does not
    // hold, or else the module import that may declare it.
    template <class Claimed>
    [[nodiscard]] std::optional<std::size_t> other_use(std::string_view word, const Claimed& claimed) const {
        for (std::size_t at = 0; at < tokens.size(); ++at) {
            if (tokens[at].is_identifier(word) && !in_proof(tokens[at]) && !claimed(at)) {
                return at;
            }
        }
        return module_import;
    }
    // What an unsafe block holds is runtime code whose safety is not
    // established, so no statement inside one is a path the verifier walks.
    // Proof syntax there would state an obligation nothing discharges, and is
    // refused rather than dropped (SPEC.md UNSAFE-003).
    [[nodiscard]] bool inside_unsafe(std::size_t offset) const {
        return std::ranges::any_of(syntax.unsafe_blocks, [offset](const UnsafeBlock& block) {
            return offset > block.body.offset && offset < block.body.end();
        });
    }

    // Every declaration and statement the scan reads, in one pass over the tokens.
    void read_declarations();

    // Whether the unit imports a module, which may declare any word.
    void find_module_import();

    // C++ first (SPEC.md 3.1, WORD-002). `contradiction name;` declares a
    // variable wherever `contradiction` names a type, and only Clang knows what
    // a name denotes. So a statement of that spelling is a claim only in a unit
    // that uses the word for nothing else, where it cannot be ordinary C++. The
    // word's uses inside laws and proofs are C++L's own and do not count; any
    // other use, a declaration in a header included, does. So are its uses
    // inside a split's arms, which belong to that split.
    void settle_contradictions();

    void settle_splits();

    void settle_unsafe();

    void settle_ghosts();

    // Proof syntax in an unsafe block, refused.
    void refuse_proof_syntax_in_unsafe();

    void read_validations();
};

} // namespace cppl::frontend::detail::recognizer
