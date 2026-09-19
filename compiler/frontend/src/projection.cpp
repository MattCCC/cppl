#include "cppl/frontend/projection.hpp"

#include <algorithm>

namespace cppl::frontend {

namespace {

void blank(std::string& buffer, const source::ByteSpan& span) {
    const std::size_t end = std::min(span.end(), buffer.size());
    for (std::size_t offset = span.offset; offset < end; ++offset) {
        if (buffer[offset] != '\n') {
            buffer[offset] = ' ';
        }
    }
}

std::string quote_path(std::string_view path) {
    std::string quoted = "\"";
    for (char character : path) {
        if (character == '\\' || character == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(character);
    }
    quoted.push_back('"');
    return quoted;
}

// Inserted text changes physical line numbering, so each insertion states the
// line it stands for and restores the numbering after itself. Diagnostics from
// inside a specification function then point at the law that produced it.
std::string line_directive(std::uint32_t line, std::string_view file) {
    if (file.empty() || line == 0) {
        return {};
    }
    return "#line " + std::to_string(line) + " " + quote_path(file) + "\n";
}

struct Edit {
    source::ByteSpan span;
    std::string replacement;
    std::optional<std::size_t> specification_index = std::nullopt;
};

struct EqualitySyntax {
    source::ByteSpan type;
    source::ByteSpan arguments;
    source::SourceLocation arguments_location;
};

// Delimit only the formal wrapper. In particular the arguments are copied as
// one C++ argument list: templates, commas, lookup and conversions belong to
// Clang. Ordinary expressions that are not this complete form are untouched.
std::optional<EqualitySyntax> equality_syntax(const TokenStream& stream, source::ByteSpan expression) {
    const auto& tokens = stream.tokens();
    std::size_t begin = static_cast<std::size_t>(
        std::lower_bound(tokens.begin(), tokens.end(), expression.offset,
                         [](const Token& token, std::size_t offset) { return token.span.offset < offset; }) -
        tokens.begin());
    std::size_t end = begin;
    while (end < tokens.size() && tokens[end].span.end() <= expression.end() &&
           tokens[end].kind != TokenKind::EndOfFile)
        ++end;
    const auto matching = [&](std::size_t from, std::string_view open, std::string_view close) {
        unsigned depth = 0;
        for (std::size_t i = from; i < end; ++i) {
            if (tokens[i].text == open)
                ++depth;
            if (tokens[i].text == close && --depth == 0)
                return i;
        }
        return end;
    };
    while (begin < end && tokens[begin].text == "(" && matching(begin, "(", ")") == end - 1) {
        ++begin;
        --end;
    }
    if (end - begin < 6 || tokens[begin].text != "Eq" || tokens[begin + 1].text != "<")
        return std::nullopt;
    unsigned angles = 1;
    std::size_t close = begin + 2;
    for (; close < end; ++close) {
        const auto token = tokens[close].text;
        if (token == "(" || token == "[") {
            close = matching(close, token, token == "(" ? ")" : "]");
            if (close == end)
                return std::nullopt;
        } else if (token == "<") {
            ++angles;
        } else if (token == ">" || token == ">>") {
            const unsigned count = token == ">>" ? 2 : 1;
            if (count > angles)
                return std::nullopt;
            if (angles <= count)
                break;
            angles -= count;
        }
    }
    if (close + 1 >= end || tokens[close + 1].text != "(" || matching(close + 1, "(", ")") != end - 1)
        return std::nullopt;
    // The final '>' can be the second character of a C++ '>>' token.
    const std::size_t type_end = tokens[close].span.offset + (tokens[close].text == ">>" && angles == 2 ? 1 : 0);
    auto location = stream.location_of(tokens[close + 1]);
    ++location.column;
    return EqualitySyntax{{tokens[begin + 2].span.offset, type_end - tokens[begin + 2].span.offset},
                          {tokens[close + 1].span.end(), tokens[end - 1].span.offset - tokens[close + 1].span.end()},
                          std::move(location)};
}

bool contains_formal_equality(const TokenStream& stream, source::ByteSpan expression) {
    const auto& tokens = stream.tokens();
    const auto start =
        std::lower_bound(tokens.begin(), tokens.end(), expression.offset,
                         [](const Token& token, std::size_t offset) { return token.span.offset < offset; });
    for (std::size_t index = static_cast<std::size_t>(start - tokens.begin());
         index + 1 < tokens.size() && tokens[index].span.offset < expression.end(); ++index) {
        if (tokens[index + 1].span.end() <= expression.end() && tokens[index].text == "Eq" &&
            tokens[index + 1].text == "<")
            return true;
    }
    return false;
}

} // namespace

Projection project(const TokenStream& stream, const Syntax& syntax, const ProjectionOptions& options) {
    const std::string_view text = stream.text();

    Projection projection;
    projection.runtime.assign(text);

    std::vector<Edit> edits;
    edits.reserve(syntax.laws.size() + syntax.proofs.size() + syntax.pure_markers.size());

    for (const PureMarker& marker : syntax.pure_markers) {
        blank(projection.runtime, marker.keyword);
        edits.push_back(Edit{marker.keyword, std::string(marker.keyword.length, ' ')});
    }

    // A declaration becomes an ordinary C++ function stating the proposition it
    // carries, emitted where the declaration stood. Everything after this point
    // in the analysis text is C++ that Clang resolves on its own.
    const auto emit = [&stream, &projection,
                       &options](std::string_view name, std::string_view parameters, const source::ByteSpan& expression,
                                 const source::SourceLocation& begin, std::uint32_t end_line,
                                 std::size_t* name_offset = nullptr, std::string* equality_name = nullptr) {
        std::string replacement = "\n";
        replacement += line_directive(begin.line, begin.file);
        replacement += "[[maybe_unused]] static bool ";
        if (name_offset != nullptr)
            *name_offset = replacement.size();
        replacement += name;
        replacement += "(";
        replacement += parameters;
        if (const auto equality = equality_syntax(stream, expression);
            equality && !contains_formal_equality(stream, equality->arguments)) {
            replacement += ");\n";
            const std::string probe = options.generated_prefix + "equality_" +
                                      std::to_string(projection.equality_probes.size()) +
                                      (options.unit_key.empty() ? "" : "_" + options.unit_key);
            projection.equality_probes.push_back({std::string(name), probe, begin});
            if (equality_name != nullptr)
                *equality_name = probe;
            replacement += line_directive(begin.line, begin.file);
            replacement += "[[maybe_unused]] static auto " + probe + "(";
            replacement += parameters;
            replacement += ") { return ([](";
            replacement += stream.spelling(equality->type);
            replacement += ", ";
            replacement += stream.spelling(equality->type);
            replacement += ") {})(\n";
            replacement += line_directive(equality->arguments_location.line, equality->arguments_location.file);
            replacement.append(equality->arguments_location.column - 1, ' ');
            replacement += stream.spelling(equality->arguments);
            replacement += "); }\n";
            replacement += line_directive(end_line, begin.file);
            return replacement;
        }
        if (contains_formal_equality(stream, expression)) {
            diagnostics::Diagnostic diagnostic;
            diagnostic.severity = diagnostics::Severity::Error;
            diagnostic.category = diagnostics::Category::UnsupportedSemantics;
            diagnostic.location = begin;
            diagnostic.message = "nested or malformed formal Eq is not supported in this proposition";
            projection.diagnostics.push_back(std::move(diagnostic));
        }
        replacement += ") { return (";
        replacement += stream.spelling(expression);
        replacement += "); }\n";
        replacement += line_directive(end_line, begin.file);
        return replacement;
    };

    for (std::size_t index = 0; index < syntax.laws.size(); ++index) {
        const LawDeclaration& law = syntax.laws[index];
        blank(projection.runtime, law.range.span);

        const Clause* proposition = law.proposition();
        if (proposition == nullptr) {
            continue;
        }

        SpecificationFunction projected{law.name, index, {}};
        std::string replacement =
            emit(law.name, stream.spelling(law.parameters), proposition->expression, law.keyword_location, law.end_line,
                 &projected.analysis_offset, &projected.equality_probe);

        // A precondition is a specification expression of the Law's own
        // parameters, so it is projected exactly like the conclusion, under a
        // generated name: the Law's name states what the Law concludes.
        if (const Clause* premise = law.premise(); premise != nullptr) {
            projected.premise_name = options.generated_prefix + "premise_" + std::to_string(index) +
                                     (options.unit_key.empty() ? "" : "_" + options.unit_key);
            replacement += emit(projected.premise_name, stream.spelling(law.parameters), premise->expression,
                                premise->location, law.end_line);
        }

        edits.push_back(Edit{law.range.span, std::move(replacement), projection.specification_functions.size()});
        projection.specification_functions.push_back(std::move(projected));
    }

    // An instantiation argument is an ordinary C++ expression written in the
    // proof's own scope, so it is projected as a function returning it. The
    // deduced return type is the type Clang gives the expression, with no
    // conversion imposed on the way out.
    const auto emit_expression = [&stream](std::string_view name, const source::ByteSpan& parameters,
                                           const source::ByteSpan& expression, const source::SourceLocation& location) {
        std::string head = "[[maybe_unused]] static auto ";
        head += name;
        head += "(";
        head += stream.spelling(parameters);
        head += ") { return (";
        // The expression's bytes are copied verbatim, so aligning the start of
        // the copy with the column it came from makes every column inside it
        // land where the author wrote it.
        if (location.column > head.size() + 1) {
            head.append(location.column - 1 - head.size(), ' ');
        }
        head += stream.spelling(expression);
        head += "); }\n";
        return head;
    };

    for (std::size_t index = 0; index < syntax.proofs.size(); ++index) {
        const ProofDeclaration& proof = syntax.proofs[index];
        blank(projection.runtime, proof.range.span);

        const std::string suffix = std::to_string(index) + (options.unit_key.empty() ? "" : "_" + options.unit_key);

        ProofFunction projected;
        projected.name = options.generated_prefix + "proof_" + suffix;
        projected.proof_index = index;

        std::string replacement = emit(projected.name, stream.spelling(proof.parameters), proof.proposition,
                                       proof.keyword_location, proof.end_line);

        for (const ProofStatement& statement : proof.statements) {
            for (const ProofArgument& argument : statement.arguments) {
                std::string name = options.generated_prefix + "argument_" + suffix + "_" +
                                   std::to_string(projected.argument_names.size());
                replacement += line_directive(argument.location.line, proof.keyword_location.file);
                replacement += emit_expression(name, proof.parameters, argument.span, argument.location);
                replacement += line_directive(proof.end_line, proof.keyword_location.file);
                projected.argument_names.push_back(std::move(name));
            }

            if (statement.kind != ProofStatementKind::Assume) {
                continue;
            }
            std::string name = options.generated_prefix + "assumption_" + suffix + "_" +
                               std::to_string(projected.assumption_names.size());
            if (contains_formal_equality(stream, statement.proposition)) {
                replacement += emit(name, stream.spelling(proof.parameters), statement.proposition,
                                    statement.proposition_location, proof.end_line);
            } else {
                replacement += line_directive(statement.proposition_location.line, proof.keyword_location.file);
                replacement +=
                    emit_expression(name, proof.parameters, statement.proposition, statement.proposition_location);
                replacement += line_directive(proof.end_line, proof.keyword_location.file);
            }
            projected.assumption_names.push_back(std::move(name));
        }

        edits.push_back(Edit{proof.range.span, std::move(replacement)});
        projection.proof_functions.push_back(std::move(projected));
    }

    // A contract is not C++, so it leaves both texts. What Clang is given
    // instead is an ordinary function per clause, emitted after the body so
    // that everything the contract can name is already declared. The
    // postcondition takes one parameter more than the function does: `result`,
    // of the declared return type.
    for (std::size_t index = 0; index < syntax.verified_functions.size(); ++index) {
        const VerifiedFunction& verified = syntax.verified_functions[index];
        blank(projection.runtime, verified.keyword);
        blank(projection.runtime, verified.clause_region);
        edits.push_back(Edit{verified.keyword, std::string(verified.keyword.length, ' ')});
        edits.push_back(Edit{verified.clause_region,
                             projection.runtime.substr(verified.clause_region.offset, verified.clause_region.length)});

        const Clause* postcondition = verified.postcondition();
        if (postcondition == nullptr) {
            continue;
        }

        const std::string suffix = std::to_string(index) + (options.unit_key.empty() ? "" : "_" + options.unit_key);
        std::string_view parameters = stream.spelling(verified.parameters);
        const std::size_t first = parameters.find_first_not_of(" \t\r\n");
        const std::size_t last = parameters.find_last_not_of(" \t\r\n");
        if (first != std::string_view::npos && parameters.substr(first, last - first + 1) == "void") {
            parameters = {};
        }
        const bool has_parameters = parameters.find_first_not_of(" \t\r\n") != std::string_view::npos;

        std::string result_parameter;
        if (has_parameters) {
            result_parameter += parameters;
            result_parameter += ", ";
        }
        result_parameter += stream.spelling(verified.return_type);
        result_parameter += " result";

        ContractFunctions projected;
        projected.function_index = index;
        projected.postcondition_name = options.generated_prefix + "ensures_" + suffix;

        std::string replacement = emit(projected.postcondition_name, result_parameter, postcondition->expression,
                                       postcondition->location, verified.body_end_line);
        for (const Clause* precondition : verified.preconditions()) {
            std::string name = options.generated_prefix + "expects_" + suffix;
            if (!projected.precondition_names.empty()) {
                name += "_" + std::to_string(projected.precondition_names.size());
            }
            replacement +=
                emit(name, parameters, precondition->expression, precondition->location, verified.body_end_line);
            projected.precondition_names.push_back(std::move(name));
        }

        replacement += line_directive(verified.body_end_line, verified.keyword_location.file);
        replacement.append(verified.body_end_column - 1, ' ');
        edits.push_back(Edit{source::ByteSpan{verified.body_end, 0}, std::move(replacement)});
        projection.contract_functions.push_back(std::move(projected));
    }

    // A loop's clauses are not C++ either. Each invariant becomes a `bool`
    // declaration at the start of the body, in the scope the loop head sees,
    // and the text after the brace resumes at its own line and column.
    for (std::size_t index = 0; index < syntax.loops.size(); ++index) {
        const LoopSpecification& loop = syntax.loops[index];
        blank(projection.runtime, loop.clause_region);
        edits.push_back(
            Edit{loop.clause_region, projection.runtime.substr(loop.clause_region.offset, loop.clause_region.length)});

        std::string replacement = "\n";
        for (std::size_t position = 0; position < loop.invariants.size(); ++position) {
            if (contains_formal_equality(stream, loop.invariants[position].expression)) {
                diagnostics::Diagnostic diagnostic;
                diagnostic.severity = diagnostics::Severity::Error;
                diagnostic.category = diagnostics::Category::UnsupportedSemantics;
                diagnostic.location = loop.invariants[position].location;
                diagnostic.message = "formal Eq in a loop invariant is not supported yet";
                projection.diagnostics.push_back(std::move(diagnostic));
            }
            LoopInvariantMarker marker;
            marker.name = options.generated_prefix + "invariant_" + std::to_string(projection.loop_invariants.size()) +
                          (options.unit_key.empty() ? "" : "_" + options.unit_key);
            marker.loop_index = index;
            marker.function_index = loop.function_index;
            marker.location = loop.invariants[position].location;

            const source::SourceLocation& at = loop.expression_locations[position];
            replacement += line_directive(at.line, loop.keyword_location.file);
            std::string head = "[[maybe_unused]] bool " + marker.name + " = (";
            if (at.column > head.size() + 1) {
                head.append(at.column - 1 - head.size(), ' ');
            }
            replacement += head;
            replacement += stream.spelling(loop.invariants[position].expression);
            replacement += ");\n";
            projection.loop_invariants.push_back(std::move(marker));
        }
        replacement += line_directive(loop.body_open_line, loop.keyword_location.file);
        replacement.append(loop.body_open_column - 1, ' ');
        edits.push_back(Edit{source::ByteSpan{loop.body_open, 0}, std::move(replacement)});
    }

    std::ranges::sort(edits, [](const Edit& lhs, const Edit& rhs) {
        if (lhs.span.offset != rhs.span.offset)
            return lhs.span.offset < rhs.span.offset;
        return lhs.span.length < rhs.span.length; // insert before replacing adjacent text
    });

    std::vector<std::size_t> declarations;
    for (const auto& marker : syntax.pure_markers)
        declarations.push_back(marker.function_offset);
    for (const auto& function : syntax.verified_functions)
        declarations.push_back(function.function_offset);
    std::ranges::sort(declarations);
    declarations.erase(std::unique(declarations.begin(), declarations.end()), declarations.end());
    std::size_t next_declaration = 0;
    std::size_t cursor = 0;
    const auto append_original = [&](std::size_t end) {
        while (next_declaration < declarations.size() && declarations[next_declaration] < end) {
            const auto original = declarations[next_declaration++];
            if (original >= cursor) {
                projection.declaration_offsets.push_back(
                    Projection::DeclarationOffset{original, projection.analysis.size() + original - cursor});
            }
        }
        projection.analysis.append(text.substr(cursor, end - cursor));
    };
    for (const Edit& edit : edits) {
        if (edit.span.offset < cursor || edit.span.end() > text.size()) {
            continue; // overlapping or out-of-range spans are never emitted
        }
        append_original(edit.span.offset);
        if (edit.specification_index.has_value()) {
            // emit() recorded the name relative to its replacement; only now
            // is its physical position in the complete analysis text known.
            projection.specification_functions[*edit.specification_index].analysis_offset += projection.analysis.size();
        }
        projection.analysis.append(edit.replacement);
        cursor = edit.span.end();
    }
    append_original(text.size());

    return projection;
}

std::optional<std::size_t> Projection::declaration_offset(std::size_t original) const {
    for (const auto& declaration : declaration_offsets) {
        if (declaration.original == original)
            return declaration.analysis;
    }
    return std::nullopt;
}

} // namespace cppl::frontend
