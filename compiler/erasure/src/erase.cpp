#include "cppl/erasure/erase.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace cppl::erasure {

namespace {

bool inside(const std::vector<source::ByteSpan>& spans, std::size_t offset) {
    return std::ranges::any_of(
        spans, [offset](const source::ByteSpan& span) { return offset >= span.offset && offset < span.end(); });
}

// A runtime-bearing declaration and the canonical C++ it must have become.
struct Lowering {
    source::ByteSpan span;
    std::string expected;
};

} // namespace

Erased erase(const frontend::TokenStream& stream, const frontend::Syntax& syntax,
             const frontend::Projection& projection, diagnostics::Engine& engine) {
    const std::string_view original = stream.text();
    const std::string_view runtime = projection.runtime;

    std::vector<source::ByteSpan> spans;
    spans.reserve(syntax.laws.size() + syntax.proofs.size() + syntax.pure_markers.size());
    for (const frontend::LawDeclaration& law : syntax.laws) {
        spans.push_back(law.range.span);
    }
    for (const frontend::ProofDeclaration& proof : syntax.proofs) {
        spans.push_back(proof.range.span);
    }
    for (const frontend::PureMarker& marker : syntax.pure_markers) {
        spans.push_back(marker.keyword);
    }
    // A verified function stays in the program; its specifier and its contract
    // do not.
    for (const frontend::VerifiedFunction& verified : syntax.verified_functions) {
        spans.push_back(verified.keyword);
        spans.push_back(verified.clause_region);
    }
    // A loop stays in the program; its specification clauses do not.
    for (const frontend::LoopSpecification& loop : syntax.loops) {
        spans.push_back(loop.clause_region);
    }
    // A claim that a path cannot occur leaves its `;` behind as an empty
    // statement, so only the words before it are erased.
    for (const frontend::PathContradiction& claim : syntax.path_contradictions) {
        spans.push_back(claim.erased);
    }

    // A refinement type is runtime-bearing: what must stand in its place is the
    // alias it means, recomputed here from the declaration so that nothing the
    // projector produced is taken on trust (SPEC.md 17.8).
    std::vector<Lowering> lowerings;
    lowerings.reserve(syntax.refinement_types.size());
    for (const frontend::RefinementType& refinement : syntax.refinement_types) {
        lowerings.push_back(Lowering{refinement.range.span, frontend::canonical_lowering(stream, refinement)});
    }
    std::ranges::sort(lowerings,
                      [](const Lowering& lhs, const Lowering& rhs) { return lhs.span.offset < rhs.span.offset; });

    Report report;
    report.erased_spans = spans.size();
    report.lowered_spans = lowerings.size();

    // Walks both texts together. Outside a lowering the two must agree byte for
    // byte, except where a proof-only span was blanked; a lowering must be
    // exactly the canonical C++ its declaration means.
    bool only_deletions = true;
    bool lowerings_canonical = true;
    std::size_t source_offset = 0;
    std::size_t runtime_offset = 0;
    const auto compare_until = [&](std::size_t source_end) {
        while (source_offset < source_end) {
            if (runtime_offset >= runtime.size()) {
                only_deletions = false;
                return;
            }
            if (runtime[runtime_offset] != original[source_offset]) {
                const bool blanked = runtime[runtime_offset] == ' ' && original[source_offset] != '\n';
                if (!blanked || !inside(spans, source_offset)) {
                    only_deletions = false;
                    return;
                }
                ++report.erased_bytes;
            }
            ++source_offset;
            ++runtime_offset;
        }
    };

    for (const Lowering& lowering : lowerings) {
        if (lowering.span.offset < source_offset || lowering.span.end() > original.size()) {
            lowerings_canonical = false; // overlapping or out of range
            break;
        }
        compare_until(lowering.span.offset);
        if (!only_deletions) {
            break;
        }
        if (runtime.substr(runtime_offset, lowering.expected.size()) != lowering.expected) {
            lowerings_canonical = false;
            break;
        }
        report.lowered_bytes += lowering.expected.size();
        runtime_offset += lowering.expected.size();
        source_offset = lowering.span.end();
    }
    if (only_deletions && lowerings_canonical) {
        compare_until(original.size());
        if (runtime_offset != runtime.size()) {
            only_deletions = false;
        }
    }

    const auto count_lines = [](std::string_view text) {
        return std::ranges::count(text, '\n');
    };
    report.lines_preserved = count_lines(original) == count_lines(runtime);
    report.only_deletions = only_deletions;
    report.lowerings_canonical = lowerings_canonical;

    if (!report.only_deletions || !report.lines_preserved || !report.lowerings_canonical) {
        diagnostics::Diagnostic diagnostic;
        diagnostic.severity = diagnostics::Severity::Error;
        diagnostic.category = diagnostics::Category::Internal;
        diagnostic.message = "erasure did not preserve the runtime program";
        diagnostic.notes.push_back(diagnostics::Note{
            "the runtime program must be the analysed program with proof-only spans removed, each "
            "runtime-bearing declaration replaced by the canonical C++ it means, and nothing else changed",
            source::SourceLocation{}});
        engine.report(std::move(diagnostic));
    }

    return Erased{runtime, report};
}

} // namespace cppl::erasure
