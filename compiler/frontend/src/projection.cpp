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
};

}  // namespace

Projection project(const TokenStream& stream,
                   const Syntax& syntax,
                   const ProjectionOptions& options) {
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
    const auto emit = [&stream](std::string_view name,
                                const source::ByteSpan& parameters,
                                const source::ByteSpan& expression,
                                const source::SourceLocation& begin,
                                std::uint32_t end_line) {
        std::string replacement = "\n";
        replacement += line_directive(begin.line, begin.file);
        replacement += "[[maybe_unused]] static bool ";
        replacement += name;
        replacement += "(";
        replacement += stream.spelling(parameters);
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

        edits.push_back(Edit{law.range.span,
                             emit(law.name, law.parameters, proposition->expression,
                                  law.keyword_location, law.end_line)});
        projection.specification_functions.push_back(SpecificationFunction{law.name, index});
    }

    // An instantiation argument is an ordinary C++ expression written in the
    // proof's own scope, so it is projected as a function returning it. The
    // deduced return type is the type Clang gives the expression, with no
    // conversion imposed on the way out.
    const auto emit_argument = [&stream](std::string_view name,
                                         const source::ByteSpan& parameters,
                                         const ProofArgument& argument) {
        std::string head = "[[maybe_unused]] static auto ";
        head += name;
        head += "(";
        head += stream.spelling(parameters);
        head += ") { return (";
        // The argument's bytes are copied verbatim, so aligning the start of
        // the copy with the column it came from makes every column inside it
        // land where the author wrote it.
        if (argument.location.column > head.size() + 1) {
            head.append(argument.location.column - 1 - head.size(), ' ');
        }
        head += stream.spelling(argument.span);
        head += "); }\n";
        return head;
    };

    for (std::size_t index = 0; index < syntax.proofs.size(); ++index) {
        const ProofDeclaration& proof = syntax.proofs[index];
        blank(projection.runtime, proof.range.span);

        const std::string suffix =
            std::to_string(index) + (options.unit_key.empty() ? "" : "_" + options.unit_key);

        ProofFunction projected;
        projected.name = options.generated_prefix + "proof_" + suffix;
        projected.proof_index = index;

        std::string replacement = emit(projected.name, proof.parameters, proof.proposition,
                                       proof.keyword_location, proof.end_line);

        for (const ProofStatement& statement : proof.statements) {
            for (const ProofArgument& argument : statement.arguments) {
                std::string name = options.generated_prefix + "argument_" + suffix + "_" +
                                   std::to_string(projected.argument_names.size());
                replacement += line_directive(argument.location.line, proof.keyword_location.file);
                replacement += emit_argument(name, proof.parameters, argument);
                replacement += line_directive(proof.end_line, proof.keyword_location.file);
                projected.argument_names.push_back(std::move(name));
            }
        }

        edits.push_back(Edit{proof.range.span, std::move(replacement)});
        projection.proof_functions.push_back(std::move(projected));
    }

    std::ranges::sort(edits, [](const Edit& lhs, const Edit& rhs) {
        return lhs.span.offset < rhs.span.offset;
    });

    std::size_t cursor = 0;
    for (const Edit& edit : edits) {
        if (edit.span.offset < cursor || edit.span.end() > text.size()) {
            continue;  // overlapping or out-of-range spans are never emitted
        }
        projection.analysis.append(text.substr(cursor, edit.span.offset - cursor));
        projection.analysis.append(edit.replacement);
        cursor = edit.span.end();
    }
    projection.analysis.append(text.substr(cursor));

    return projection;
}

}  // namespace cppl::frontend
