#include "cppl/lsp/hover.hpp"

#include "cppl/clang/editor.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/projected_file.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::lsp {

namespace {

std::string code_block(std::string_view code) {
    return "```cpp\n" + std::string(code) + "\n```\n";
}

// The text between two offsets, without the space around it.
std::string written(std::string_view text, std::size_t from, std::size_t to) {
    if (from >= text.size() || to <= from) {
        return {};
    }
    std::string_view span = text.substr(from, std::min(to, text.size()) - from);
    while (!span.empty() && std::isspace(static_cast<unsigned char>(span.back())) != 0) {
        span.remove_suffix(1);
    }
    while (!span.empty() && std::isspace(static_cast<unsigned char>(span.front())) != 0) {
        span.remove_prefix(1);
    }
    return std::string(span);
}

// The byte offset of a location in `text`, which the location was lexed from.
std::optional<std::size_t> offset_of(std::string_view text, const source::SourceLocation& location) {
    if (location.line == 0 || location.column == 0) {
        return std::nullopt;
    }
    std::size_t line_start = 0;
    for (std::uint32_t line = 1; line < location.line; ++line) {
        const std::size_t newline = text.find('\n', line_start);
        if (newline == std::string_view::npos) {
            return std::nullopt;
        }
        line_start = newline + 1;
    }
    return line_start + location.column - 1;
}

bool names_at(std::string_view text, const source::SourceLocation& location, const std::string& name,
              std::size_t offset) {
    const std::optional<std::size_t> start = offset_of(text, location);
    return start.has_value() && offset >= *start && offset < *start + name.size() &&
           text.substr(*start, name.size()) == name;
}

std::optional<CpplDeclaration> describe_assumption(const std::vector<frontend::ProofStatement>& statements,
                                                   std::string_view text, std::size_t offset) {
    for (const frontend::ProofStatement& statement : statements) {
        if (statement.kind == frontend::ProofStatementKind::Assume &&
            names_at(text, statement.reference_location, statement.reference, offset)) {
            CpplDeclaration declaration;
            declaration.kind = CpplDeclaration::Kind::Assumption;
            declaration.name = statement.reference;
            declaration.name_location = statement.reference_location;
            declaration.first_line = statement.location.line;
            declaration.last_line = statement.location.line;
            declaration.markdown =
                "**assumption** `" + statement.reference + "`\n\n" +
                code_block("assume " + statement.reference + " : " +
                           written(text, statement.proposition.offset, statement.proposition.end()) + ";") +
                "\nA premise the proof supposes from here to the end of the block it is written in.";
            return declaration;
        }
        for (const frontend::ProofArm& arm : statement.arms) {
            if (std::optional<CpplDeclaration> found = describe_assumption(arm.statements, text, offset)) {
                return found;
            }
        }
    }
    return std::nullopt;
}

} // namespace

std::string describe_cpp(const clangbridge::Description& description, std::string_view declared_in) {
    std::string markdown = "**" + description.kind + "** `" + description.qualified_name + "`\n\n";
    if (!description.declaration.empty()) {
        markdown += code_block(description.declaration);
    }
    std::vector<std::string> facts;
    if (!description.type.empty() && description.declaration.find(description.type) == std::string::npos) {
        facts.push_back("Type: `" + description.type + "`");
    }
    if (!description.value.empty()) {
        facts.push_back("Value: `" + description.value + "`");
    }
    if (description.size.has_value() && description.alignment.has_value()) {
        facts.push_back("Size: " + std::to_string(*description.size) + " bytes, alignment " +
                        std::to_string(*description.alignment));
    }
    if (!declared_in.empty()) {
        facts.push_back("Declared in `" + std::string(declared_in) + "`");
    }
    for (const std::string& fact : facts) {
        markdown += "\n" + fact + "  ";
    }
    if (!description.documentation.empty()) {
        markdown += "\n\n---\n\n" + description.documentation;
    }
    while (!markdown.empty() && (markdown.back() == '\n' || markdown.back() == ' ')) {
        markdown.pop_back();
    }
    return markdown;
}

std::optional<std::string> describe_implicit(const clangbridge::Description& description) {
    const std::string type = description.type.empty() ? std::string() : " It has type `" + description.type + "`.";
    if (description.name == "result") {
        return "**result**\n\nThe value the function returns, named in its postcondition." + type;
    }
    if (description.name == "self") {
        return "**self**\n\nThe value the refinement type's predicate is stated of." + type;
    }
    return std::nullopt;
}

std::optional<CpplDeclaration> describe_cppl(const frontend::TokenStream& tokens, const frontend::Syntax& syntax,
                                             std::string_view text, std::size_t name_offset) {
    for (const frontend::LawDeclaration& law : syntax.laws) {
        if (!names_at(text, law.name_location, law.name, name_offset)) {
            continue;
        }
        CpplDeclaration declaration;
        declaration.kind = law.trusted ? CpplDeclaration::Kind::TrustedLaw : CpplDeclaration::Kind::Law;
        declaration.name = law.name;
        declaration.name_location = law.name_location;
        declaration.first_line = law.keyword_location.line;
        declaration.last_line = law.end_line;
        declaration.markdown = std::string(law.trusted ? "**trusted law**" : "**law**") + " `" + law.name + "`\n\n" +
                               code_block(written(text, law.range.span.offset, law.range.span.end()));
        if (law.trusted) {
            declaration.markdown += "\nAn explicit assumption: nothing proves it, and every claim that rests on it is "
                                    "reported relative to it.";
        }
        return declaration;
    }
    for (const frontend::ProofDeclaration& proof : syntax.proofs) {
        if (proof.inline_law.has_value() || !names_at(text, proof.name_location, proof.name, name_offset)) {
            continue;
        }
        const std::size_t body = text.find('{', proof.proposition.end());
        CpplDeclaration declaration;
        declaration.kind = CpplDeclaration::Kind::Proof;
        declaration.name = proof.name;
        declaration.name_location = proof.name_location;
        declaration.first_line = proof.keyword_location.line;
        declaration.last_line = proof.end_line;
        declaration.markdown = "**proof** `" + proof.name + "`\n\n" +
                               code_block(written(text, proof.range.span.offset,
                                                  body == std::string_view::npos ? proof.range.span.end() : body));
        return declaration;
    }
    for (const frontend::ProofDeclaration& proof : syntax.proofs) {
        if (std::optional<CpplDeclaration> assumption = describe_assumption(proof.statements, text, name_offset)) {
            return assumption;
        }
    }
    for (const frontend::RefinementType& refinement : syntax.refinement_types) {
        const std::optional<source::ByteSpan> name = declared_name(tokens, refinement.range.span, refinement.name);
        if (!name.has_value() || name_offset < name->offset || name_offset >= name->end()) {
            continue;
        }
        const std::string base = written(text, refinement.base.offset, refinement.base.end());
        CpplDeclaration declaration;
        declaration.kind = CpplDeclaration::Kind::RefinementType;
        declaration.name = refinement.name;
        for (const frontend::Token& token : tokens.tokens()) {
            if (token.span.offset == name->offset) {
                declaration.name_location = tokens.location_of(token);
                break;
            }
        }
        declaration.first_line = refinement.keyword_location.line;
        declaration.last_line = refinement.end_line;
        declaration.markdown = "**refinement type** `" + refinement.name + "`\n\n";
        declaration.markdown += code_block(written(text, refinement.range.span.offset, refinement.range.span.end()));
        declaration.markdown += "\nA value of `" + base + "` for which `";
        declaration.markdown += written(text, refinement.predicate.offset, refinement.predicate.end());
        declaration.markdown += "` holds. It erases to `" + base + "`: nothing of the refinement exists at run time.";
        return declaration;
    }
    for (const frontend::VerifiedFunction& verified : syntax.verified_functions) {
        if (name_offset < verified.function_offset ||
            name_offset >= verified.function_offset + verified.function_name.size() ||
            text.substr(verified.function_offset, verified.function_name.size()) != verified.function_name) {
            continue;
        }
        const std::size_t from =
            verified.template_header.length != 0 ? verified.template_header.offset : verified.keyword.offset;
        const std::size_t to =
            verified.clause_region.length != 0 ? verified.clause_region.end() : verified.parameters.end() + 1;
        CpplDeclaration declaration;
        declaration.kind = CpplDeclaration::Kind::VerifiedFunction;
        declaration.name = verified.function_name;
        declaration.name_location = verified.function_location;
        declaration.first_line = verified.keyword_location.line;
        declaration.last_line = verified.body_end_line;
        declaration.markdown =
            "**verified function** `" + verified.function_name + "`\n\n" + code_block(written(text, from, to));
        return declaration;
    }
    return std::nullopt;
}

} // namespace cppl::lsp
