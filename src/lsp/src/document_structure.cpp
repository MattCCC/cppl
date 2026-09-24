// Folding and selection ranges (editor_view.hpp): the structure Clang parsed
// and the C++L structure the recognizer found, traced back to the text as
// written. Nothing here reads C++ or C++L structure from the text itself.

#include "cppl/clang/editor.hpp"
#include "cppl/frontend/structure.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/editor_view.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/projected_file.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace cppl::lsp {

namespace {

// Where an extent of the unit's main file was written, when the source map
// traces its first and its last byte there.
std::optional<source::ByteSpan> written_span(const ProjectedFile& file, const clangbridge::Extent& extent) {
    if (extent.end.offset <= extent.begin.offset) {
        return std::nullopt;
    }
    const std::optional<std::size_t> begin = file.to_written(extent.begin.offset);
    const std::optional<std::size_t> last = file.to_written(extent.end.offset - 1);
    if (!begin.has_value() || !last.has_value() || *last < *begin) {
        return std::nullopt;
    }
    return source::ByteSpan{*begin, *last + 1 - *begin};
}

bool is_blank(char character) {
    return character == ' ' || character == '\t' || character == '\r' || character == '\f' || character == '\v';
}

// Whether only space stands between the start of its line and `offset`.
bool starts_its_line(const std::string& text, std::size_t offset) {
    while (offset > 0 && is_blank(text[offset - 1])) {
        --offset;
    }
    return offset == 0 || text[offset - 1] == '\n';
}

// Whether only space stands between `offset` and the end of its line.
bool ends_its_line(const std::string& text, std::size_t offset) {
    while (offset < text.size() && is_blank(text[offset])) {
        ++offset;
    }
    return offset == text.size() || text[offset] == '\n';
}

class Folds {
  public:
    Folds(const std::string& text, bool line_folding_only)
        : text_(text),
          mapper_(text),
          line_folding_only_(line_folding_only) {}

    [[nodiscard]] std::uint32_t line_of(std::size_t offset) const {
        return mapper_.byte_offset_to_position(offset).line;
    }

    // Whole lines, from `first` through `last`.
    void lines(std::uint32_t first, std::uint32_t last, const char* kind) {
        if (last > first) {
            folds_.push_back(FoldingRange{first, std::nullopt, last, std::nullopt, kind});
        }
    }

    // Between the `{` at `open` and the `}` at `close`.
    void braces(std::size_t open, std::size_t close) {
        const Position start = mapper_.byte_offset_to_position(open + 1);
        const Position end = mapper_.byte_offset_to_position(close);
        if (line_folding_only_) {
            if (end.line > start.line + 1) {
                folds_.push_back(FoldingRange{start.line, std::nullopt, end.line - 1, std::nullopt, ""});
            }
        } else if (end.line > start.line) {
            folds_.push_back(FoldingRange{start.line, start.character, end.line, end.character, ""});
        }
    }

    // Each block comment, and each run of line comments on lines of their own.
    // Code on a comment's first or last line is never folded away with it.
    void comments(const std::vector<source::ByteSpan>& comments) {
        for (std::size_t index = 0; index < comments.size(); ++index) {
            const source::ByteSpan& comment = comments[index];
            const std::uint32_t first = line_of(comment.offset);
            if (text_.compare(comment.offset, 2, "/*") == 0) {
                const std::uint32_t last = line_of(comment.end() - 1);
                lines(first, ends_its_line(text_, comment.end()) || last == 0 ? last : last - 1, "comment");
                continue;
            }
            if (!starts_its_line(text_, comment.offset)) {
                continue;
            }
            std::size_t run = index;
            while (run + 1 < comments.size() && text_.compare(comments[run + 1].offset, 2, "//") == 0 &&
                   starts_its_line(text_, comments[run + 1].offset) &&
                   line_of(comments[run + 1].offset) == line_of(comments[run].offset) + 1) {
                ++run;
            }
            lines(first, line_of(comments[run].offset), "comment");
            index = run;
        }
    }

    // Consecutive `#include` lines, each span one directive, together.
    void includes(std::vector<source::ByteSpan> directives) {
        std::ranges::sort(directives, {}, &source::ByteSpan::offset);
        std::size_t index = 0;
        while (index < directives.size()) {
            std::size_t run = index;
            while (run + 1 < directives.size() &&
                   line_of(directives[run + 1].offset) == line_of(directives[run].offset) + 1) {
                ++run;
            }
            lines(line_of(directives[index].offset), line_of(directives[run].offset), "imports");
            index = run + 1;
        }
    }

    // Outer folds first, and each once.
    [[nodiscard]] std::vector<FoldingRange> sorted() && {
        std::ranges::sort(folds_, [](const FoldingRange& lhs, const FoldingRange& rhs) {
            return std::tie(lhs.start_line, rhs.end_line) < std::tie(rhs.start_line, lhs.end_line);
        });
        const auto [first, last] = std::ranges::unique(folds_, [](const FoldingRange& lhs, const FoldingRange& rhs) {
            return lhs.start_line == rhs.start_line && lhs.end_line == rhs.end_line;
        });
        folds_.erase(first, last);
        return std::move(folds_);
    }

  private:
    const std::string& text_;
    PositionMapper mapper_;
    bool line_folding_only_;
    std::vector<FoldingRange> folds_;
};

} // namespace

std::vector<FoldingRange> EditorView::folding_ranges(bool line_folding_only) const {
    if (main_ == nullptr) {
        return {};
    }
    const ProjectedFile& file = *main_;
    Folds folds(file.text(), line_folding_only);
    if (unit_ != nullptr) {
        std::vector<source::ByteSpan> includes;
        for (const clangbridge::Fold& fold : unit_->folds()) {
            if (fold.kind == clangbridge::Fold::Kind::Conditional) {
                // Up to the line before the directive that ends the branch.
                const std::optional<std::size_t> begin = file.to_written(fold.extent.begin.offset);
                const std::optional<std::size_t> end = file.to_written(fold.extent.end.offset);
                if (begin.has_value() && end.has_value() && *end > *begin) {
                    const std::uint32_t last = folds.line_of(*end);
                    folds.lines(folds.line_of(*begin), last == 0 ? 0 : last - 1, "region");
                }
                continue;
            }
            const std::optional<source::ByteSpan> span = written_span(file, fold.extent);
            if (!span.has_value()) {
                continue;
            }
            if (fold.kind == clangbridge::Fold::Kind::Include) {
                includes.push_back(*span);
            } else {
                folds.braces(span->offset, span->end() - 1);
            }
        }
        folds.includes(std::move(includes));
    }
    for (const frontend::Block& block : frontend::blocks(file.syntax())) {
        if (block.kind == frontend::Block::Kind::Braces) {
            folds.braces(block.span.offset, block.span.end() - 1);
        } else {
            folds.lines(folds.line_of(block.span.offset), folds.line_of(block.span.end() - 1), "");
        }
    }
    folds.comments(file.tokens().comments());
    return std::move(folds).sorted();
}

std::vector<Range> EditorView::selection(const Position& position) const {
    if (main_ == nullptr) {
        return {Range{position, position}};
    }
    const ProjectedFile& file = *main_;
    const std::string& text = file.text();
    const PositionMapper mapper(text);
    const std::size_t offset = std::min(mapper.position_to_byte_offset(position), text.size());
    const auto holds = [offset](const source::ByteSpan& span) {
        return span.length != 0 && span.offset <= offset && offset <= span.end();
    };

    std::vector<source::ByteSpan> spans;
    // The token under the position, or else the one it is just past.
    const frontend::Token* under = nullptr;
    for (const frontend::Token& token : file.tokens().tokens()) {
        if (token.kind != frontend::TokenKind::EndOfFile && holds(token.span) &&
            (under == nullptr || token.span.offset == offset)) {
            under = &token;
        }
    }
    if (under != nullptr) {
        spans.push_back(under->span);
    }
    std::ranges::copy_if(file.tokens().comments(), std::back_inserter(spans), holds);
    std::ranges::copy(frontend::enclosing(file.syntax(), offset), std::back_inserter(spans));
    if (unit_ != nullptr) {
        if (const std::optional<std::size_t> analysis = file.to_analysis(offset)) {
            for (const clangbridge::Extent& extent : unit_->enclosing(*analysis)) {
                if (const std::optional<source::ByteSpan> span = written_span(file, extent)) {
                    spans.push_back(*span);
                }
            }
        }
    }

    // Each span holds the one before it, and is larger.
    std::ranges::stable_sort(spans, {}, &source::ByteSpan::length);
    std::vector<Range> chain;
    source::ByteSpan inner;
    for (const source::ByteSpan& span : spans) {
        if (!holds(span)) {
            continue;
        }
        if (!chain.empty() && (span.length == inner.length || span.offset > inner.offset || span.end() < inner.end())) {
            continue;
        }
        chain.push_back(Range{mapper.byte_offset_to_position(span.offset), mapper.byte_offset_to_position(span.end())});
        inner = span;
    }
    if (chain.empty()) {
        chain.push_back(Range{position, position});
    }
    return chain;
}

} // namespace cppl::lsp
