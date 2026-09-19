#include "cppl/erasure/erase.hpp"

#include <algorithm>
#include <vector>

namespace cppl::erasure {

namespace {

bool inside(const std::vector<source::ByteSpan>& spans, std::size_t offset) {
    return std::ranges::any_of(spans, [offset](const source::ByteSpan& span) {
        return offset >= span.offset && offset < span.end();
    });
}

}  // namespace

Erased erase(const frontend::TokenStream& stream,
             const frontend::Syntax& syntax,
             const frontend::Projection& projection,
             diagnostics::Engine& engine) {
    const std::string_view original = stream.text();
    const std::string_view runtime = projection.runtime;

    std::vector<source::ByteSpan> spans;
    spans.reserve(syntax.laws.size() + syntax.pure_markers.size());
    for (const frontend::LawDeclaration& law : syntax.laws) {
        spans.push_back(law.range.span);
    }
    for (const frontend::PureMarker& marker : syntax.pure_markers) {
        spans.push_back(marker.keyword);
    }

    Report report;
    report.erased_spans = spans.size();

    bool only_deletions = runtime.size() == original.size();
    if (only_deletions) {
        for (std::size_t offset = 0; offset < original.size(); ++offset) {
            if (runtime[offset] == original[offset]) {
                continue;
            }
            const bool blanked = runtime[offset] == ' ' && original[offset] != '\n';
            if (!blanked || !inside(spans, offset)) {
                only_deletions = false;
                break;
            }
            ++report.erased_bytes;
        }
    }

    const auto count_lines = [](std::string_view text) {
        return std::ranges::count(text, '\n');
    };
    report.lines_preserved = count_lines(original) == count_lines(runtime);
    report.only_deletions = only_deletions;

    if (!report.only_deletions || !report.lines_preserved) {
        diagnostics::Diagnostic diagnostic;
        diagnostic.severity = diagnostics::Severity::Error;
        diagnostic.category = diagnostics::Category::Internal;
        diagnostic.message = "erasure did not preserve the runtime program";
        diagnostic.notes.push_back(diagnostics::Note{
            "the runtime program must be the analysed program with proof-only spans removed "
            "and nothing else changed",
            source::SourceLocation{}});
        engine.report(std::move(diagnostic));
    }

    return Erased{runtime, report};
}

}  // namespace cppl::erasure
