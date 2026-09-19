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
    edits.reserve(syntax.laws.size() + syntax.pure_markers.size());

    for (const PureMarker& marker : syntax.pure_markers) {
        blank(projection.runtime, marker.keyword);
        edits.push_back(Edit{marker.keyword, std::string(marker.keyword.length, ' ')});
    }

    for (std::size_t index = 0; index < syntax.laws.size(); ++index) {
        const LawDeclaration& law = syntax.laws[index];
        blank(projection.runtime, law.range.span);

        const Clause* proposition = law.proposition();
        if (proposition == nullptr) {
            continue;
        }

        std::string name = options.specification_prefix + std::to_string(index);
        if (!options.unit_key.empty()) {
            name += "_" + options.unit_key;
        }

        std::string replacement = "\n";
        replacement += line_directive(law.keyword_location.line, law.keyword_location.file);
        replacement += "[[maybe_unused]] static bool ";
        replacement += name;
        replacement += "(";
        replacement += stream.spelling(law.parameters);
        replacement += ") { return (";
        replacement += stream.spelling(proposition->expression);
        replacement += "); }\n";
        replacement += line_directive(law.end_line, law.keyword_location.file);

        edits.push_back(Edit{law.range.span, std::move(replacement)});
        projection.specification_functions.push_back(SpecificationFunction{std::move(name), index});
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
