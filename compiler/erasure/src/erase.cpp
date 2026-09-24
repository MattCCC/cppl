#include "cppl/erasure/erase.hpp"

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::erasure {

namespace {

// The proof-only spans as disjoint intervals in source order, so the walk below
// decides whether a byte was erased without searching every span for it.
std::vector<source::ByteSpan> disjoint(std::vector<source::ByteSpan> spans) {
    std::ranges::sort(spans,
                      [](const source::ByteSpan& lhs, const source::ByteSpan& rhs) { return lhs.offset < rhs.offset; });
    std::vector<source::ByteSpan> merged;
    for (const source::ByteSpan& span : spans) {
        if (span.length == 0) {
            continue;
        }
        if (!merged.empty() && span.offset <= merged.back().end()) {
            merged.back().length = std::max(merged.back().end(), span.end()) - merged.back().offset;
            continue;
        }
        merged.push_back(span);
    }
    return merged;
}

// What an erased byte must read as in the runtime program: a space, except that
// a newline stays so no line below it moves.
char blanked(char original) {
    return original == '\n' ? '\n' : ' ';
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
    // `unsafe` marks a boundary and nothing else: the word goes, and the
    // declaration or the block's statements stay exactly as written (SPEC.md
    // ERASE-003, Annex M).
    for (const frontend::UnsafeFunction& function : syntax.unsafe_functions) {
        spans.push_back(function.keyword);
    }
    for (const frontend::UnsafeBlock& block : syntax.unsafe_blocks) {
        spans.push_back(block.keyword);
    }
    // Ghost state leaves whole, its initializer with it (SPEC.md GHOST-001,
    // ERASE-011, Annex M).
    for (const frontend::GhostDeclaration& ghost : syntax.ghost_declarations) {
        spans.push_back(ghost.erased);
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
    // statement, so only the words before it are erased. A claim inside a
    // split's arm goes with that split.
    for (const frontend::PathContradiction& claim : syntax.path_contradictions) {
        if (!claim.split.has_value()) {
            spans.push_back(claim.erased);
        }
    }

    // A refinement type is runtime-bearing: what must stand in its place is the
    // alias it means, recomputed here from the declaration so that nothing the
    // projector produced is taken on trust (SPEC.md 17.8).
    std::vector<Lowering> lowerings;
    lowerings.reserve(syntax.refinement_types.size() + syntax.path_splits.size());
    for (const frontend::RefinementType& refinement : syntax.refinement_types) {
        lowerings.push_back(Lowering{refinement.range.span, frontend::canonical_lowering(stream, refinement)});
    }
    // A case split on a runtime path becomes an empty statement, so whatever
    // statement it was the body of still has one (SPEC.md CASE-017, ERASE-016).
    for (const frontend::PathCaseSplit& split : syntax.path_splits) {
        lowerings.push_back(Lowering{split.span, frontend::erased_split(stream, split)});
    }
    std::ranges::sort(lowerings,
                      [](const Lowering& lhs, const Lowering& rhs) { return lhs.span.offset < rhs.span.offset; });

    Report report;
    report.erased_spans = spans.size();
    report.lowered_spans = lowerings.size();

    // Walks both texts together. Outside a lowering the two must agree byte for
    // byte, except inside a proof-only span, where every byte must be blank and
    // only its newlines may remain; a lowering must be exactly the canonical C++
    // its declaration means. Checking that a span is blank, and not merely that
    // what changed lies inside one, is what stops proof syntax the projector
    // failed to remove from reaching Clang as runtime code (SPEC.md ERASE-005,
    // ERASE-007, ERASE-016; TRUST.md TCB-ERASE-006).
    const std::vector<source::ByteSpan> erased = disjoint(spans);
    bool only_deletions = true;
    bool spans_erased = std::ranges::all_of(
        erased, [&original](const source::ByteSpan& span) { return span.end() <= original.size(); });
    bool lowerings_canonical = true;
    std::size_t next_erased = 0; // the first erased span not wholly before the walk
    const auto erased_at = [&erased, &next_erased](std::size_t offset) {
        while (next_erased < erased.size() && erased[next_erased].end() <= offset) {
            ++next_erased;
        }
        return next_erased < erased.size() && erased[next_erased].offset <= offset;
    };
    std::size_t source_offset = 0;
    std::size_t runtime_offset = 0;
    const auto compare_until = [&](std::size_t source_end) {
        while (source_offset < source_end) {
            if (runtime_offset >= runtime.size()) {
                only_deletions = false;
                return;
            }
            const char kept = runtime[runtime_offset];
            const char written = original[source_offset];
            if (erased_at(source_offset)) {
                if (kept == blanked(written)) {
                    if (kept != written) {
                        ++report.erased_bytes;
                    }
                } else if (kept == written) {
                    spans_erased = false; // proof-only text left in the program
                } else {
                    only_deletions = false;
                    return;
                }
            } else if (kept != written) {
                only_deletions = false;
                return;
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
        // A runtime-bearing declaration is never also proof-only: its bytes are
        // replaced, not blanked, so a span reaching into it cannot be checked.
        if (erased_at(lowering.span.offset) ||
            (next_erased < erased.size() && erased[next_erased].offset < lowering.span.end())) {
            lowerings_canonical = false;
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
    report.spans_erased = spans_erased;
    report.lowerings_canonical = lowerings_canonical;

    if (!report.preserved()) {
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
